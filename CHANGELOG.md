# Changelog

## [0.0.0] - 2026-09-17

### Added

- Added head tracking for Ghostrunner on Steam: head rotation and lean move the
  first person view while the mouse or controller keeps aiming.
- Added crosshair compensation, so the game's crosshair sits on the point the
  sword, the shuriken and the grapple are aimed at, following head rotation and
  lean at every range. The sensory-boost gauge and the dash charges move with it.
- Added gameplay gating, so tracking pauses in menus, the pause screen,
  cutscenes, photo mode, death and loading.
- Added a lean clamp that holds the view off walls, so leaning does not put the
  eye inside level geometry.
- Added field-of-view compensation, so a slide, a dash or the rift does not
  change how far a head movement moves the view.
- Added centring of a windowed game on the monitor it opens on, once the game
  has finished placing its window. A fullscreen or borderless window, and one
  the game already centred, are left alone.
