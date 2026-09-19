// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

// What the player's own character is doing this frame, read off the live object
// graph through the engine's reflection data.
//
// The pawn's camera component is PlayerCharacter::FollowCameraRef, and the
// drawn view is built from it: APlayerController::GetPlayerViewPoint reaches
// the possessed pawn's CalcCamera, which returns that component's world
// transform because AActor::bFindCameraComponentWhenViewTarget is set. The
// sword, the shuriken and the grapple all resolve their aim from the same
// component rather than from the view the render caller is handed, so the aim
// ray is read here and the view hook never writes to it.
namespace gr_ht::player_rig {

struct Transform {
    bool Valid = false;
    cameraunlock::unreal::FVector Position{0.0, 0.0, 0.0};
    cameraunlock::unreal::FVector Forward{1.0, 0.0, 0.0};
};

struct Snapshot {
    // The PlayerCharacter the controller possesses, or 0 for any other pawn (or
    // none). Photo mode swaps in a BP_PhotoModePawn_C, which fails this and so
    // closes the gate without a flag of its own.
    std::uintptr_t Pawn = 0;
    // PlayerCharacter::FollowCameraRef. Gameplay is drawn from it; a cutscene,
    // a killcam or the photo-mode camera is drawn from somewhere else.
    Transform Camera;
    bool Dead = false;
    // PlayerCharacter::CurrentFOV and ::DefaultFOV, in degrees. DefaultFOV is
    // the game's own field-of-view setting and is the zoom compensation's base;
    // CurrentFOV is where the slide, dash and rift curves have driven it.
    bool HaveFov = false;
    float CurrentFov = 0.0f;
    float DefaultFov = 0.0f;
};

// Game thread only. `controller` is the player controller the view hook holds.
Snapshot Read(std::uintptr_t controller);

}  // namespace gr_ht::player_rig
