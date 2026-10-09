# DCVR development

DCVR is the optional PC VR experiment on Dylan's `dcvr` fork branch. Normal desktop play remains
the default. The target is a small Norune room or dungeon floor with stereo depth, six-degree
head tracking, and conventional movement. Physical swings, hands, and VR combat are later work.

## Implemented development tools

- `DC_ENABLE_OPENXR=ON` builds `dcvr_probe` with the pinned Khronos OpenXR SDK 1.1.63 loader.
  It reports runtime/extensions, headset tracking capabilities, stereo view sizes, Vulkan
  requirements and the **runtime-selected** GPU. It uses `xrCreateVulkanInstanceKHR` and
  `xrGetVulkanGraphicsDevice2KHR` and checks Chronicle's renderer requirements. It never selects
  a different GPU to hide incompatibility. No window, game data, saves, session or runtime-setting
  changes are needed. Exit 0 means graphics prerequisites; exit 1 means unavailable/incompatible;
  exit 2 means invalid arguments. Standard output is JSON; standard error holds loader logs.
- `dcvr_room` is a standalone calibration scene using **Chronicle's actual Vulkan renderer**.
  It negotiates instance creation, GPU selection and device creation through OpenXR enable2,
  creates a session, LOCAL/VIEW reference spaces and separate eye swapchains, and submits stereo
  projection layers with predicted poses/FOV. It needs no game data or save directory. Its room
  is 6×6×3 metres, with a one-metre floor grid, a half-metre red cube and a one-metre blue cube.
  It is a preparation step for the Norune/dungeon milestone, not Dark Cloud gameplay in VR.
- The session loop begins on READY, ends on STOPPING and exits on instance/session loss. A
  stopped session can start again on READY. Hidden frames and invalid positional/orientation
  tracking submit zero layers. Each acquired image is waited, drawn, returned to Vulkan's
  `COLOR_ATTACHMENT_OPTIMAL` layout and released before submission. Exceptions close begun
  frames without submitting a half-finished pair. Runtime loss requires restarting the tool;
  automatic recreation is not implemented.
- The calibration camera recenters position and yaw while preserving head pitch/roll and IPD.
  LOCAL reference changes take effect at their specified predicted time and reset that anchor.
  WASD moves at one metre/second, Q/E turns 45 degrees, R recenters and Esc exits. Movement uses
  a body heading and is enabled only when OpenXR is focused and the controls window has keyboard
  focus. Virtual walking stays inside room bounds; head movement and cube collision are not
  constrained. This camera is calibration code, not a replacement for game collision or controls.
- Display-list eye overrides replace one camera's projection and compose an eye transform after
  its interpolated view. Depth-tested world sprites are unprojected through the recorded camera
  and reprojected through the eye. HUD and other cameras retain their mapping. Canonical eye
  overrides and invalid matrices are refused.
- Pose/FOV math converts OpenXR's right-handed +Y-up/-Z-forward metres into Chronicle camera
  coordinates (+Y down/+Z forward), with an asymmetric reverse-Z Vulkan projection. Unit tests
  use synthetic poses. Live pose acquisition is implemented in the calibration tool but has
  not been exercised against a connected headset.
- `--stereo-screenshot NEW_DIR`, with `--offscreen --frames N`, captures the final recorded
  game's frame as left/right PNGs. It replays drawing only; it does not execute gameplay,
  texture uploads or depth queries per eye. The capture uses camera 0, a synthetic 64 mm IPD,
  symmetric 90-degree fields of view, and **provisional** 10 game units/metre. `capture.json`
  records those assumptions. The directory must be new and its parent must exist.

**Dark Cloud gameplay is not connected to the headset yet.** The calibration submission path is
implemented and tested with an injected OpenXR boundary and real Vulkan images. A real headset
session has not been verified. Synthetic captures and successful frame submission receipts do
not establish comfort, visual correctness in the Quest, or the game's world scale.

## First-time Quest 2 setup on Windows

The Quest runs the headset display and tracking; this PC runs the game and sends its pictures
through Meta Horizon Link. OpenXR is the interface the game will use to the active VR runtime.
SteamVR is not required for the initial Meta route.

1. Charge and update the Quest 2. Download and install the **Meta Horizon Link PC app** from
   [Meta's setup page](https://www.meta.com/help/quest/509273027107091/), then sign in yourself.
   Accept Windows elevation prompts locally if setup requests them. Do not send credentials to an agent.
2. Dylan chose **Air Link**. Connect the headset to a suitable 5 GHz Wi-Fi network, ideally with the PC
   on Ethernet to the same router. Enable Air Link inside the headset, select this PC, and Pair.
   Compare the code shown in the headset with the PC app, confirm, then Launch.
3. Establish a stable Link home/dashboard before testing DCVR. In the PC app's Settings → General,
   inspect OpenXR Runtime. If Meta is not active, select **Set Meta Horizon Link as active** for
   this project. This changes the system's active runtime: the diagnostic never does it for you.
4. Run `tools/windows/diagnose-vr.ps1` and inspect the receipt. An unavailable runtime means PC
   setup/runtime registration still needs work. An unavailable headset means Link must be
   connected and launched in the headset. A successful capability probe is not yet a rendered test.
5. Run the bounded calibration tool below and have Dylan check the picture inside the Quest.

The headset pairing prompts and what you see in the headset require Dylan. No developer-mode
APK deployment or Unity installation is needed for this native Windows development slice.
Meta currently lists NVIDIA RTX 50-series GPUs as supported, but that does not qualify the
application or a particular cable/driver/session. See
[Meta PC requirements](https://www.meta.com/en-gb/help/quest/140991407990979/) and
[Meta Link development setup](https://developers.meta.com/vr/documentation/unity/unity-link/).

## Build and reproduce

Follow [WINDOWS.md](WINDOWS.md) for the llvm-mingw/SDL/Vulkan dependencies. Use a separate output:

```powershell
$deps = 'C:/Dylan/_scratch/chronicle-win' # existing portable dependencies on Dylan's PC
./tools/windows/configure.ps1 -Root $deps -BuildDirectory "$PWD/build/dcvr" -Tests -OpenXR -BuildType RelWithDebInfo
./tools/windows/build.ps1 -Root $deps -BuildDirectory "$PWD/build/dcvr" -Tests -OpenXR
./tools/windows/diagnose-vr.ps1
$env:DC_GFX_OFFSCREEN = '1'
ctest --test-dir build/dcvr -j 1 --output-on-failure
```

The `-Tests -OpenXR` build also includes `dcvr_tests`; use `ctest -R DCVR` for its focused suite.
Omit `-OpenXR` to build the desktop game without fetching or linking OpenXR. The stereo math and
capture are renderer tools and remain buildable without an SDK/runtime. On other build systems
the CMake option is `-DDC_ENABLE_OPENXR=ON`. Only the Windows route has been built for this slice.

After Air Link is paired and launched, run the first live calibration test from PowerShell:

```powershell
./build/dcvr/dcvr_room.exe --frames 600 --seconds 60 > ./build/room-session.json 2> ./build/room-session.log
```

Focus the small controls window for keyboard input. The large eye-rendering window stays hidden;
there is no desktop eye mirror yet. The first valid tracked head pose centers the room. Check
left/right alignment and depth, lean/turn without keyboard movement, then recenter and try
conventional movement. No game save or install is used. Exit 0 means that some calibration frames
were submitted; it never means Dylan saw or approved them. The JSON keeps `headset_validated=false`
for that reason. `--frames` counts begun XR frames; `--seconds` limits the event loop's wall time,
with up to two seconds to process a requested exit. These limits cannot interrupt a blocked driver
or runtime API call. Other exits follow the probe's convention (1 unavailable/failure, 2 bad arguments).

Without a Quest, capture the room using synthetic poses and the ordinary desktop Vulkan path:

```powershell
./build/dcvr/dcvr_room.exe --synthetic "$PWD/build/calibration-stereo"
```

The directory must be new. This writes left/right PNGs at 960×720 and a metadata receipt, with a
synthetic 64 mm IPD and 90-degree FOV. No OpenXR calls run in this mode. The live tool uses the
runtime's eye sizes, FOV and IPD. Both paths reuse a cleared display target sequentially, copying
each live eye to its own acquired swapchain image before drawing the next. GPU waits deliberately
serialize this prototype. There is no temporal feedback, water or shared shadow target in this room;
this does not solve those effects or establish headset frame-time performance.

For a bounded Norune capture, first copy a save folder to an isolated test directory. Never use
the active play save. Use owned extracted PAL data read-only, e.g.:

```powershell
$env:DC_AUDIO = 'off'
./build/dcvr/darkcloud.exe --data 'C:/path/to/owned-PAL-data' --save 'C:/path/to/copied-test-save' `
  --offscreen --jump edit:0 --fast-load --frames 260 --width 960 --height 720 `
  --stereo-screenshot "$PWD/build/norune-stereo"
```

The existing R/R2 first-person view and keyboard walking are desktop foundations. They have not
been remapped to the headset. Synthetic captures can also use `--input` as described in [PC.md](PC.md).

## Remaining integration gates

1. Run the calibration tool on a real Quest/Air Link session with Dylan and qualify runtime
   negotiation, image orientation, stereo, positional/rotational tracking, recentering and exit.
2. Connect the implemented runtime/session backend to a bounded Norune/dungeon game mode.
   Keep simulation on Chronicle's fixed tick clock, rather than tying gameplay speed to headset
   refresh. Canonical game side effects must remain once per logic tick; display replays acquire
   late poses. The calibration tool does not run the game clock or alter gameplay timing.
3. Make eye color/depth, shared-depth shadows, frame grabs, water and temporal feedback independent.
   Game captures reuse the display target sequentially and reject frames needing a canonical
   base. Separate calibration swapchains are not a complete per-eye game history solution.
4. Expand or replace CPU culling before recording. A wider FOV or late head turn cannot recover
   discarded geometry. Capture camera 0 is a developer assumption, not reliable world-camera
   classification across menus, reflections and every game mode.
5. Connect the player's first-person camera/controls, suppress camera easing/cuts where necessary,
   calibrate scale and recentering, and handle head movement through walls. Place HUD/menus at an
   explicit readable depth. Screen-space markers without depth are still unmoved.
6. Validate one bounded room/floor in the Quest with Dylan: eye orientation, stereo depth, lean,
   turn, conventional movement, menus, loss of tracking and exit. Loading, fishing, Georama and
   the rest of the game remain outside that first playable slice.

Relevant specification entries:
[Vulkan enable2](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_vulkan_enable2.html),
[runtime GPU selection](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetVulkanGraphicsDevice2KHR.html),
[device creation](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateVulkanDeviceKHR.html),
[frame timing](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html),
[frame submission](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndFrame.html).

## Development checkpoint — 2026-10-08

Started from `a27772646f33e5a5795eb7dc8f01f78b1fe2b46c`, on `dcvr` tracking `fork/dcvr`.
The Windows baseline built and passed 276 tests. The first implementation built with OpenXR
enabled and disabled; 285 tests passed, including nine new CPU/GPU stereo cases. The cases
cover asymmetric FOV edges, reverse depth, metre/basis conversion, rotation, invalid inputs,
depth-dependent disparity, sprites, isolated camera selection, cuts/interpolation, HUD, canonical
image/depth preservation and suppression of repeated texture uploads. Release-style builds
disable Vulkan validation layers, so these results do not establish validation-layer cleanliness.

With owned PAL data and copied saves, bounded first-person captures succeeded in Norune
(260 ticks) and Wise Owl Forest (820 ticks), at 960×720 on the RTX 5090. The Norune desktop PNG
was byte-identical to the pre-change build (SHA-256
`17DD3AEC6DDE882044F42D066240B39A32B3F1FDF1F60A09E5D6B4F98BB9FB15`). The synthetic eyes are
different images. Visual inspection found exposed edges/gaps with the wider projection; CPU
culling and view-dependent backgrounds remain unresolved. None of this is headset validation.

Local receipts are under this worktree's ignored `build/desktop`, `build/dcvr`, and
`build/validation` directories. Game data, play installs, shortcuts, real saves and matching
`ps2/` sources were not modified.

Dylan chose **Air Link** and said this is his first VR-development setup. The PC has an active
2.5 Gbps Ethernet link. Meta Horizon Link **1.115.0** was installed from Meta's signed installer
(SHA-256 `7367AEB0E202B0A50976993FB2750E75A5215F9A0966FC8FB4E84807BA3B57F1`); the installer
registered Meta as the previously absent OpenXR runtime. No agent registry switch was performed.
The probe now enumerates `XR_KHR_vulkan_enable2` and creates an OpenXR instance on the
**Oculus 1.208.0** runtime. It then returns `XR_ERROR_FORM_FACTOR_UNAVAILABLE` because no headset
system is available. Dylan reports he has signed in and enabled developer settings, but is not
ready to add Quest 2. Air Link pairing, Vulkan negotiation past headset discovery, headset rendering,
tracking and comfort have not been verified. Development continues without changing runtime settings
or requiring pairing now.

The second implementation checkpoint adds the reusable OpenXR session/frame backend, renderer negotiation
hooks and `dcvr_room` calibration runner. Automated evidence covers lifecycle/failure ordering,
predicted timestamps, raw composition poses, deferred reference changes, recenter/movement math,
real Vulkan eye transfers and renderer creation through a provided Vulkan 1.3 device. The SDK
boundary is injected for those session tests: no real XR session is implied. Live probe and room
startup still fail at headset discovery, before device/session creation. The synthetic calibration
room was rendered and visually inspected. Windows builds with OpenXR on and off succeeded:
the enabled suite passed 304 tests, including 19 new DCVR lifecycle/math/GPU cases; the disabled
desktop suite passed 285. After the final failure-stop change, all 19 DCVR cases passed again.
A new copied-save Norune run (260 ticks, 960×720) retained the exact baseline PNG hash above.
Unicode-path room captures succeeded, existing capture directories were refused, and invalid
numeric arguments returned exit 2. Local receipts include `build/validation/session-tests.log`,
`session-focused-final.log`, `desktop-session-tests.log`, `openxr-room.json` and `norune-session.png`.
Real saves, play installs, shortcuts, runtime settings and matching sources remain untouched.
