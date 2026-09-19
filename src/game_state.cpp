// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <atomic>
#include <cmath>

#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace gr_ht::game_state {

namespace {

namespace ue = ::cameraunlock::unreal;

// The drawn view and the pawn's camera component agree to within the camera
// manager's own shakes in gameplay; a cutscene, the death killcam or a scripted
// camera actor is metres away or pointed elsewhere. Ghostrunner shakes the
// camera hard on a landing and rolls it through a wallrun, so the angle
// allowance has to clear both - but a cutscene camera in this game is a
// different room, not a different tilt, so the distance is what actually
// separates the two cases.
constexpr double kMaxViewOffsetCm = 60.0;
constexpr double kMaxViewAngleDeg = 35.0;

constexpr double kRadiansToDegrees = 57.29577951308232;

ue_vm::ResolveRetry g_retry;
bool g_resolved = false;

std::uintptr_t g_staticsCdo = 0;
ue_call::Function g_isGamePaused;       // GameplayStatics::IsGamePaused
std::size_t g_worldGameInstanceOffset = 0;

struct ControllerFields {
    std::uintptr_t Class = 0;
    ue_reflect::FieldInfo Cursor;
};
ControllerFields g_controllerFields;

// BP_GameManager_C is the game instance, and carries the pause-menu flag. It is
// resolved against the class it was first seen on, like every other field here.
struct GameInstanceFields {
    std::uintptr_t Class = 0;
    bool Have = false;
    ue_reflect::FieldInfo PauseMenu;    // bIsPauseMenuOpened
};
GameInstanceFields g_gameInstance;

bool Resolve(std::uintptr_t controller) {
    if (g_resolved) return true;
    if (!g_retry.Due() || !ue_vm::Ready()) return false;

    g_staticsCdo = ue_call::DefaultObject("GameplayStatics");
    const std::uintptr_t world = ue_call::WorldOf(controller);
    ue_reflect::FieldInfo gameInstance;
    if (!g_staticsCdo || !world ||
        !ue_reflect::FindPropertyInChain(ue_call::ClassOf(world), "OwningGameInstance", gameInstance) ||
        gameInstance.Size != sizeof(std::uintptr_t))
        return false;
    if (!g_isGamePaused.Resolve("GameplayStatics", "IsGamePaused",
                                {{"WorldContextObject", sizeof(std::uintptr_t)}, {"ReturnValue", 1}}))
        return false;

    g_worldGameInstanceOffset = gameInstance.Offset;
    g_resolved = true;
    Log::Line("gate: resolved (IsGamePaused; World.OwningGameInstance=+0x%zx)",
              g_worldGameInstanceOffset);
    return true;
}

void RefreshControllerFields(std::uintptr_t controller) {
    const std::uintptr_t cls = ue_call::ClassOf(controller);
    if (!cls || cls == g_controllerFields.Class) return;
    g_controllerFields = ControllerFields{};
    g_controllerFields.Class = cls;
    ue_reflect::FindPropertyInChain(cls, "bShowMouseCursor", g_controllerFields.Cursor);
    Log::Line("gate: controller class %s (cursor flag %s)", ue::ObjectName(cls).c_str(),
              g_controllerFields.Cursor.BoolMask ? "found" : "MISSING");
}

// The game instance, with its pause-menu flag resolved. 0 means the chain broke
// this frame, which the caller treats as blocking.
std::uintptr_t GameInstance(std::uintptr_t controller) {
    const std::uintptr_t world = ue_call::WorldOf(controller);
    std::uintptr_t gi = 0;
    if (!world || !ue::SafeReadPtr(world + g_worldGameInstanceOffset, gi) || !gi) return 0;
    const std::uintptr_t cls = ue_call::ClassOf(gi);
    if (!cls) return 0;
    if (cls != g_gameInstance.Class) {
        g_gameInstance = GameInstanceFields{};
        g_gameInstance.Class = cls;
        g_gameInstance.Have =
            ue_reflect::FindPropertyInChain(cls, "bIsPauseMenuOpened", g_gameInstance.PauseMenu) &&
            g_gameInstance.PauseMenu.BoolMask != 0;
        Log::Line("gate: game instance %s (pause-menu flag %s)", ue::ClassName(gi).c_str(),
                  g_gameInstance.Have ? "found" : "MISSING");
    }
    return g_gameInstance.Have ? gi : 0;
}

// False when the flag could not be read, which the caller treats as blocking.
bool AskFlag(const ue_call::Function& fn, std::uintptr_t self, std::size_t index, bool& out,
             std::uintptr_t context = 0) {
    if (!self) return false;
    ue_call::Frame frame(fn);
    if (context) frame.Set(0, context);
    if (!frame.Call(self)) return false;
    out = (frame.Get<std::uint8_t>(index) & 1u) != 0;
    return true;
}

}  // namespace

Verdict Evaluate(std::uintptr_t controller, const player_rig::Snapshot& rig,
                 const ue::FVector& cleanLocation, const ue::FRotator& cleanRotation) {
    Verdict v;
    if (!rig.Pawn || !rig.Camera.Valid || !Resolve(controller)) {
        v.Why = Blocker::NoPlayer;
        return v;
    }
    if (rig.Dead) {
        v.Why = Blocker::Dead;
        return v;
    }

    RefreshControllerFields(controller);
    bool cursor = true;
    if (!ue_reflect::ReadBool(controller, g_controllerFields.Cursor, cursor) || cursor) {
        v.Why = Blocker::Cursor;
        return v;
    }

    // Two independent pause signals, both blocking. The engine flag covers a
    // time dilation of zero from anywhere; the game's own covers its pause menu,
    // which it opens before it stops the clock.
    bool menu = true;
    const std::uintptr_t gi = GameInstance(controller);
    if (!gi || !ue_reflect::ReadBool(gi, g_gameInstance.PauseMenu, menu) || menu) {
        v.Why = Blocker::Paused;
        return v;
    }
    bool paused = true;
    if (!AskFlag(g_isGamePaused, g_staticsCdo, 1, paused, controller) || paused) {
        v.Why = Blocker::Paused;
        return v;
    }

    const ue::FVector& cam = rig.Camera.Position;
    const double dx = cleanLocation.X - cam.X, dy = cleanLocation.Y - cam.Y, dz = cleanLocation.Z - cam.Z;
    v.ViewOffsetCm = std::sqrt(dx * dx + dy * dy + dz * dz);
    const ue::FVector viewFwd = ue::QuatRotateVec(
        ue::QuatFromEulerDeg(cleanRotation.Pitch, cleanRotation.Yaw, cleanRotation.Roll),
        ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector& camFwd = rig.Camera.Forward;
    double dot = viewFwd.X * camFwd.X + viewFwd.Y * camFwd.Y + viewFwd.Z * camFwd.Z;
    dot = dot > 1.0 ? 1.0 : (dot < -1.0 ? -1.0 : dot);
    v.ViewAngleDeg = std::acos(dot) * kRadiansToDegrees;
    if (!(v.ViewOffsetCm <= kMaxViewOffsetCm) || !(v.ViewAngleDeg <= kMaxViewAngleDeg)) {
        v.Why = Blocker::NotFirstPerson;
        return v;
    }

    v.Why = Blocker::None;
    v.InGameplay = true;
    return v;
}

const char* BlockerName(Blocker b) {
    switch (b) {
        case Blocker::None:           return "gameplay";
        case Blocker::NoPlayer:       return "no player character (menu, loading, photo mode)";
        case Blocker::Dead:           return "player dead";
        case Blocker::Cursor:         return "mouse cursor up (menu)";
        case Blocker::Paused:         return "game paused";
        case Blocker::NotFirstPerson: return "view is not the pawn's camera (cutscene or killcam)";
    }
    return "unknown";
}

void LogTransitions(const Verdict& v) {
    static std::atomic<int> s_lastWhy{-1};
    const int why = static_cast<int>(v.Why);
    if (s_lastWhy.exchange(why) != why)
        Log::Line("gate: %s (view offset %.1fcm %.1fdeg)", BlockerName(v.Why), v.ViewOffsetCm,
                  v.ViewAngleDeg);
}

}  // namespace gr_ht::game_state
