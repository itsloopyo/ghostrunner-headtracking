// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// The game's own crosshair, moved onto the point the aim ray stops on.
//
// Ghostrunner draws its crosshair in the middle of the screen, which is right
// only while the drawn view is the aim. With the head turned or leaned it is
// not, so the mod moves the HUD panel holding the crosshair, and the
// sensory-boost gauge and dash charges laid out around it, by the screen offset
// of the projected aim point, through the engine's own
// UWidget::SetRenderTranslation. The mark on screen is the game's, and no
// second one is drawn.
namespace gr_ht::reticle {

// Game thread. `controller` is the player controller, `pawn` the player
// character. `valid` false puts the crosshair back where the game laid it out,
// which is what every frame without a usable projection does. `ndcX` / `ndcY`
// are -1..1, x right and y up.
void Publish(std::uintptr_t controller, std::uintptr_t pawn, bool valid, float ndcX, float ndcY);

}  // namespace gr_ht::reticle
