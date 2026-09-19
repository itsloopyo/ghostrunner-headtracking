// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <windows.h>

#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"

namespace gr_ht::hotkeys {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::input::ChordGuarded;
using cameraunlock::input::NavGuarded;

// Virtual-key codes. The nav-cluster defaults and the Ctrl+Shift chord cluster
// (T/Y/U/G/H/J) are the fleet-wide bindings from AGENTS.md.
//
// Ghostrunner's own action mappings leave Y, U, G, H and J unbound - the only
// letter of the cluster it uses is T, for the upgrade menu, which the fleet
// keeps free anyway - so the three chords here are the standard ones with no
// substitutions.
constexpr int kVkEnd      = 0x23;
constexpr int kVkPageUp   = 0x21;
constexpr int kVkY        = 0x59;
constexpr int kVkG        = 0x47;
constexpr int kVkH        = 0x48;

// How often the poller samples the keyboard, in milliseconds.
constexpr unsigned kPollIntervalMs = 16;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

}  // namespace

void ToggleTracking() {
    const bool enabled = !view_hook::TrackingEnabled();
    view_hook::SetTrackingEnabled(enabled);
    Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
}

void CycleTrackingMode() {
    const TrackingMode mode = g_session->CycleMode();
    const char* name = mode == TrackingMode::RotationOnly ? "rotation only"
                     : mode == TrackingMode::PositionOnly ? "position only"
                                                          : "rotation and position";
    Log::Line("hotkey: tracking mode -> %s", name);
}

void ToggleYawMode() {
    const bool worldSpaceYaw = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(worldSpaceYaw);
    Log::Line("hotkey: yaw mode %s", worldSpaceYaw ? "world" : "local");
}

void Register(const Config& config, Session& session) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Nav-cluster defaults. Suppressed when Ctrl+Shift is held so the chord
    // path is the sole trigger.
    //
    // One action per key. The poller fires EVERY entry bound to a code, so a
    // second action on a code already taken would run alongside the first on a
    // single press - and the one key the INI does let a player change sits
    // directly under a comment naming End and Page Up, which are fixed. A
    // collision is refused with a line rather than bound anyway.
    std::vector<int> navKeys{kVkEnd, kVkPageUp};
    const auto addNav = [&](int vk, const char* key, void (*action)()) {
        if (std::find(navKeys.begin(), navKeys.end(), vk) != navKeys.end()) {
            Log::Line("hotkey: [Hotkeys] %s=0x%02X is a key another action already has - "
                      "%s is not bound to it this session", key, vk, key);
            return;
        }
        navKeys.push_back(vk);
        g_poller->AddHotkey(vk, NavGuarded(action));
    };

    g_poller->AddHotkey(kVkEnd,    NavGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(kVkPageUp, NavGuarded([] { CycleTrackingMode(); }));
    addNav(config.yaw_mode_key, "YawMode", &ToggleYawMode);

    // Ctrl+Shift chord alternatives, for keyboards with no nav cluster.
    g_poller->AddHotkey(kVkY, ChordGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(kVkG, ChordGuarded([] { CycleTrackingMode(); }));
    g_poller->AddHotkey(kVkH, ChordGuarded([] { ToggleYawMode(); }));

    g_poller->Start(kPollIntervalMs);
}

void Stop() {
    if (g_poller) g_poller->Stop();
}

}  // namespace gr_ht::hotkeys
