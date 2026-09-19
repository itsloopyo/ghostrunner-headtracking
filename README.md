# Ghostrunner Head Tracking

![Ghostrunner running with this mod](https://raw.githubusercontent.com/itsloopyo/ghostrunner-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Ghostrunner that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse or controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Ghostrunner](https://store.steampowered.com/app/1139900/Ghostrunner/) on Steam. Steam is the only store supported: the mod recognizes the game by its executable, and it has no profile for another store's copy, so it stays dormant there.
- A head tracking source that can send the OpenTrack UDP protocol: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or a VR headset, or a phone app that sends it directly.
- Windows 10 or 11, 64-bit.

The mod recognizes the game build it was made for by its executable: the Steam build whose main menu footer reads `0.42507.61`. On a build it does not know, it writes a line to `GhostrunnerHeadTracking.log` and stays dormant, and the game runs exactly as it ships.

## Installation

### Standalone Installer

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/ghostrunner-headtracking/releases) page. There is no release yet; until there is, build from source.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

The installer resolves the game folder itself. Point it at one to install into a copy it would not pick:

```powershell
# Environment variable
$env:GHOSTRUNNER_PATH = "D:\SteamLibrary\steamapps\common\Ghostrunner"
.\install.cmd

# Or as an argument
.\install.cmd "D:\SteamLibrary\steamapps\common\Ghostrunner"
```

### Manual Installation

The payload goes next to the game's shipping executable, in `Ghostrunner\Binaries\Win64\` under the game folder, beside `Ghostrunner-Win64-Shipping.exe`. Not beside the `Ghostrunner.exe` in the game's root, which is only a launcher.

No mod manager deploys this mod: they place files into one fixed subtree per game, and this one has to land beside the shipping exe, which is why there is a single installer ZIP and no Nexus page.

From the extracted ZIP:

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` into that folder and rename it to `winmm.dll`. `WINMM.dll` is an import the shipping executable already has, so it is the proxy name the loader has to use here.
2. Copy `plugins\GhostrunnerHeadTracking.asi` into the same folder.

`HeadTracking.ini` and `GhostrunnerHeadTracking.log` are written to that folder on first launch.

## Setting Up OpenTrack

1. Open OpenTrack.
2. Set **Output** to `UDP over network`.
3. Open its options and set the address to `127.0.0.1` and the port to `4242`.
4. Pick an **Input** (see below), then press **Start**.

Centering is done in the tracker: OpenTrack's Center bind, the CENTER button in a phone app, or SteamVR's reset.

### VR Headset Setup

1. Connect the headset over Air Link, Virtual Desktop, or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on `UDP over network`, `127.0.0.1:4242`.

### Webcam Setup

1. Set OpenTrack's **Input** to `neuralnet tracker`, which uses the webcam alone and needs no markers, clips, or IR hardware.
2. Pick your camera in the tracker options and set the resolution and frame rate it runs at.
3. Leave **Output** on `UDP over network`, `127.0.0.1:4242`, then press **Start**.

### Phone App Setup

This mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone tracker is usable here if it sends that protocol itself, or ships a PC-side companion that does. Check your app against that before anything else.

For an app that does send it, what decides the wiring is how much filtering it does on the phone before the packet leaves. An app that filters on-device can point straight at this PC's LAN address on port `4242`. A raw or lightly filtered feed sent direct will jitter, and that app should go through OpenTrack instead so its filters can clean the feed up first. The test is quicker than the theory: send direct, hold your head still, and if the view drifts or shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It filters on-device, so it can send direct. Any app that filters enough noise works the same way.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a tracker running on this same PC that sends to the LAN address instead of `127.0.0.1`, because the mod classifies the transport rather than the machine.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord          |
|---------------------|-------------|----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H` |

Ghostrunner binds `T` to the upgrade menu and leaves the rest of that keyboard cluster alone, so the chords above collide with nothing in the game. It does read `Ctrl` as crouch and `Shift` as dash, so holding a chord down also crouches and dashes.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

**Toggle yaw mode** switches which axis head yaw turns about. Horizon-locked is the default: yaw goes about the world up-axis, so looking at the floor and turning your head pans across it. Camera-local turns about the camera's own up-axis instead, which leans the horizon when the camera is pitched steeply. It applies for the session and is not written back to the file.

## Configuration

`HeadTracking.ini` sits next to the game exe, in `Ghostrunner\Binaries\Win64\`, and is written with the defaults on first launch. Delete it to get the defaults back.

```ini
[Network]
; UDP port the tracker sends to. 4242 is the OpenTrack default.
Port=4242

[Tracking]
; Smoothing, 0.0 (none) to 1.0 (heaviest). LocalSmoothing applies to a
; tracker sending to 127.0.0.1; RemoteSmoothing to any other address,
; including this PC's own LAN address.
LocalSmoothing=0.00
RemoteSmoothing=0.15

[General]
; 1 = head yaw turns about the world's up axis (horizon stays level).
; 0 = about the camera's own up axis. Page Down (or Ctrl+Shift+H)
; toggles this for the session.
WorldSpaceYaw=1

[Camera]
; Stop a positional lean from putting the view inside walls.
CollisionEnabled=1
; Distance held off a surface, in centimetres (5 to 40).
CollisionMargin=10.0

[Hotkeys]
; Virtual-key codes. End (toggle tracking), Page Up (cycle tracking
; mode) and the Ctrl+Shift chords (Y, G, H) are fixed.
YawMode=0x22
```

### Field of view

Ghostrunner has its own field of view slider in the video options. The mod reads it while the game runs, so a change to it takes effect on the next frame without a restart.

Head tracking moves the picture by the same amount whatever field of view the game is drawing at. A slide, a dash and the rift all pull the field of view away from your setting, and that magnifies or shrinks everything in the frame, head movement included. The mod scales the head pose by the ratio between the field of view being drawn and the one your slider is set to, so a given head movement moves the view as far mid-dash as it does walking. Head roll is left alone, because a tilt of the picture is the same tilt at any field of view. The `fov:` line in the log carries both values and the factor between them, and reads `factor 1.0000` in ordinary play.

### Window placement

A windowed game is moved once to the center of the desktop work area on the monitor it opened on, after its window has stopped moving. That is the screen minus the taskbar, so the picture sits a little above the middle of the glass. A fullscreen or borderless window is left where it is, and so is one the game already put there. Nothing is moved on a game build the mod has no profile for. The `window:` line in the log says which of those happened.

## Troubleshooting

`GhostrunnerHeadTracking.log`, beside the game exe, records the build the mod matched, whether the hook installed, the tracker link, and every change in whether head tracking is allowed and why. Read it first.

**Mod not loading:**

- Check that `winmm.dll` and `GhostrunnerHeadTracking.asi` are both in `Ghostrunner\Binaries\Win64\`. A copy next to the `Ghostrunner.exe` launcher in the game's root folder is never loaded.
- No `GhostrunnerHeadTracking.log` at all means the loader never ran; run `install.cmd` again and let it resolve the path itself.
- On a game build this release does not know, the log says so and the mod stays dormant on purpose rather than hooking against addresses that have moved.

**No tracking response:**

- You are in a menu, the pause menu, a cutscene, photo mode, dead, or loading. By design the view is left alone in all of those, and the log names which one.
- Something else has the tracker port. `link: UDP 4242 waiting-for-port` is the mod waiting for it, and the `udp: Failed to bind UDP port 4242` line above it carries the reason Windows gave. Error 10048 is another program already on the port, usually a game left running - close it and the mod takes the port on its next retry, under a second later, without you restarting anything.
- `link: UDP 4242 listening` with no `receiving` line after it means nothing is sending to the port. Check the tracker is running and pointed at this machine on the port in `HeadTracking.ini`.
- Check tracking is not switched off with `End` or `Ctrl+Shift+Y`.

**Jittery or unstable tracking:**

- A phone sending a raw feed direct is the usual cause. Route it through OpenTrack and let its filters clean it up, or raise `RemoteSmoothing`.
- On a webcam, poor or uneven lighting makes the neuralnet tracker's output noisy. Light your face evenly and avoid a bright window behind you.

**Yaw feels wrong when looking up or down at extreme angles:**

- Press `Page Down` to switch yaw mode. Horizon-locked, the default, turns head yaw about the world's up axis, so the horizon stays level however steeply the camera is pitched. Camera-local turns it about the camera's own up axis instead, which tilts the horizon as you turn while looking up or down. Press it again to go back.

**Leaning into a wall stops short:**

- By design. The lean is cut to the room the level leaves, holding the view `CollisionMargin` centimetres off the surface, and the log writes `lean-clamp: holding the view off geometry` when it does. `CollisionEnabled=0` turns that off, at the cost of seeing through walls when you lean into them.

**The crosshair moves when I turn my head:**

- By design. It moves to stay on the point the sword, the shuriken and the grapple are actually pointed at, which is no longer the middle of the screen once your head is turned or leaning. The sensory-boost gauge and the dash charges drawn around it move with it.

**Known limitations:**

- Only the Steam build named under Requirements has been tested.
- Ghostrunner has no multiplayer mode, so the mod covers single player alone.

## Updating

Download the new release and run `install.cmd` again. `HeadTracking.ini` is left alone, so your settings carry over.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files and its ini and logs. The loader is only removed if the installer put it there; `uninstall.cmd /force` removes it anyway.

## Building from Source

Requires Visual Studio 2022 with the C++ workload, CMake 3.20 or newer, and [pixi](https://pixi.sh).

```powershell
git clone --recursive https://github.com/itsloopyo/ghostrunner-headtracking.git
cd ghostrunner-headtracking
pixi run build
pixi run test
pixi run package
```

`pixi run package` writes the installer ZIP to `release\`. `pixi run deploy` builds and copies the result into every detected game install for a dev loop.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

Third-party components keep their own licenses, listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- [One More Level](https://store.steampowered.com/app/1139900/Ghostrunner/) for Ghostrunner, published by 505 Games.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, the loader this mod ships with.
- [OpenTrack](https://github.com/opentrack/opentrack) for the tracking protocol and the trackers that speak it.
- [MinHook](https://github.com/TsudaKageyu/minhook) by TsudaKageyu, used for the engine hooks.
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared head tracking library behind every mod in this series.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by One More Level or 505 Games. Use at your own risk.
