// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

#include "player_rig.h"

// When head tracking is allowed to touch the view at all. Polled every render
// frame from the live controller, never latched.
//
// Gameplay is the frame drawn from the player character's own camera component,
// alive, with no cursor up, the game not paused and the pause menu closed.
// Anything unreadable reads as the blocking answer.
//
// A cutscene, the death killcam and photo mode need no flag of their own. All
// three hand the view to a camera that is not the pawn's, which the last check
// catches, and photo mode also possesses a BP_PhotoModePawn_C that the rig
// refuses outright. A level load has no pawn for the same reason.
//
// Ghostrunner has no multiplayer mode - the main menu offers the campaign,
// Kill Run, Wave Mode, settings, credits and quit, all played alone - so there
// is no session gate here.
namespace gr_ht::game_state {

enum class Blocker {
    None,
    NoPlayer,
    Dead,
    Cursor,
    Paused,
    NotFirstPerson,
};

struct Verdict {
    bool InGameplay = false;
    Blocker Why = Blocker::NoPlayer;
    // How far the drawn view sits from the pawn's camera component, for the log.
    double ViewOffsetCm = -1.0;
    double ViewAngleDeg = -1.0;
};

// `cleanLocation` / `cleanRotation` are the view the game built this frame,
// before any head pose.
Verdict Evaluate(std::uintptr_t controller, const player_rig::Snapshot& rig,
                 const cameraunlock::unreal::FVector& cleanLocation,
                 const cameraunlock::unreal::FRotator& cleanRotation);

void LogTransitions(const Verdict& v);

const char* BlockerName(Blocker b);

}  // namespace gr_ht::game_state
