# DCVR development

DCVR is the optional PC VR experiment on Dylan's `dcvr` fork branch. Normal desktop play remains
the default. The target is a small Norune room or dungeon floor with stereo depth, six-degree
head tracking, and conventional movement. **Third-person play and its camera are on the critical
path for the first playable milestone.** First-person elements retain their original limited role;
a first-person conversion is not the primary design. Physical swings, hands, and a combat redesign
are later work.

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
- `dcvr_game` connects **normal third-person Norune walking** to that backend. It records and
  renders one canonical game frame per logic tick, then replays drawing for each runtime eye
  using the predicted head/eye poses and asymmetric FOV. Toan keeps the existing keyboard/SDL
  gamepad movement, collision and follow camera. The recentered head pose composes after the
  interpolated game camera; it never injects a stick or mouse input. F9 recenters, Esc exits.
  This separate opt-in executable leaves ordinary `darkcloud` free of an OpenXR dependency.
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

**The third-person game submission path is implemented; a real headset session remains unverified.**
The synthetic game path and injected OpenXR/Vulkan tests qualify software behavior only. They
do not establish comfort, visual correctness in Quest, world scale or headset frame rate.

## Dylan's first headset test

Pair and **Launch Air Link inside Quest**, leaving Meta Horizon Link running on the PC. Use
Link → Use Air Link → this PC → Pair, compare the pairing code with the PC, Confirm, then Launch.
The exact navigation label varies with Horizon OS; the current steps are on
[Meta's Air Link setup page](https://www.meta.com/help/quest/509273027107091/).

1. Double-click `tools/windows/test-dcvr-room.cmd`. It probes prerequisites first, then opens
   the calibration room for up to 60 seconds. Keep its keyboard window focused. R recenters;
   Esc exits. Check stereo, head rotation and leaning before proceeding to the game.
2. Double-click `tools/windows/test-dcvr-norune.cmd`. It creates a **new copied save** for every
   run and opens Norune for up to two minutes. Focus the DCVR window. WASD or an ordinary SDL
   gamepad moves Toan; mouse/right stick controls the existing camera. The copied config enables
   wheel zoom; middle click resets that zoom. **F9** recenters the head and **Esc** exits.
3. First look around without walking. Then take a few steps with Toan visible, try camera orbit
   and a small zoom change, and approach a wall slowly. Report framing, scale, follow-camera
   motion and any missing geometry separately from whether the app merely submitted frames.

Receipts and logs are in `build/dcvr/vr-tests/<new run>/`. Norune's `save/dcvr-report.json` records
runtime status, canonical ticks, submitted frames, eye renders and camera/avatar movement.
The launcher terminates only its owned test child if it exceeds the duration plus 45 seconds.
It never switches the active runtime. No install, shortcut or real save is altered.

This is a **world-only scene prototype**: HUD, screen-space composites, town shadows, water
surfaces/refraction and depth-of-field are omitted. Loading, cuts, first-person, menus, talk,
events, fishing, Georama and other maps submit no game layer. The PC status panel identifies
loading/unsupported views; exit and restart if you leave the supported walking scene. Quest
controllers have no game action bindings yet; use the keyboard or an ordinary gamepad.

The first render is 960×720 per eye, blitted to the runtime's recommended swapchain size.
Native-resolution performance is not qualified. The PC operator mirror updates at **2 Hz** and
is not the headset refresh rate. The initial scale is **provisional 10 game units/metre**; change
it with `test-dcvr.ps1 -Mode Norune -UnitsPerMetre 15` for a separate comparison run. IPD is always
the runtime's eye separation. The game rig's pitch, following, easing and collision recovery
remain visible to the viewer; head lean can penetrate walls because head collision is unfinished.
Billboards, view-dependent lighting and world markers are not fully qualified for independent eyes.

For another checkout, supply `-BuildDirectory`, owned `-DataDirectory` and a candidate
`-SeedSave` folder. `-Mode Synthetic -Frames 300 -InputScript FILE` exercises the same game
camera/display seam without any OpenXR calls, recording three synthetic stereo display frames
per canonical tick and neutral/turned/leaning final eye PNGs in a fresh `capture` directory.

## First-time Quest 2 setup on Windows

The Quest runs the headset display and tracking; this PC runs the game and sends its pictures
through Meta Horizon Link. OpenXR is the interface the game will use to the active VR runtime.
SteamVR is not required for the initial Meta route.

1. Charge and update the Quest 2. Download and install the **Meta Horizon Link PC app** from
   [Meta's setup page](https://www.meta.com/help/quest/509273027107091/), then sign in yourself.
   Accept Windows elevation prompts locally if setup requests them. Do not send credentials to an agent.
2. Dylan chose **Air Link**. Connect the headset to a suitable 5 GHz Wi-Fi network, ideally with the PC
   on Ethernet to the same router. Open Link and Use Air Link inside the headset, select this PC, and Pair.
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

Reproduce the moving third-person game diagnostic with the tracked input sequence:

```powershell
pwsh -NoProfile -File tools/windows/test-dcvr.ps1 -Mode Synthetic -Frames 300 `
  -InputScript "$PWD/tools/dcvr/norune-walk.input"
```

Its default paths use this PC's existing owned data and isolated `save-baseline` seed. Supply
`-DataDirectory` and `-SeedSave` elsewhere. Omitting the input file selects a tracked neutral script.

For a bounded Norune capture, first copy a save folder to an isolated test directory. Never use
the active play save. Use owned extracted PAL data read-only, e.g.:

```powershell
$env:DC_AUDIO = 'off'
./build/dcvr/darkcloud.exe --data 'C:/path/to/owned-PAL-data' --save 'C:/path/to/copied-test-save' `
  --offscreen --jump edit:0 --fast-load --frames 260 --width 960 --height 720 `
  --stereo-screenshot "$PWD/build/norune-stereo"
```

The existing R/R2 first-person view and keyboard walking are desktop foundations for those
specific modes. They have not been remapped to the headset and are not the primary VR camera.
Synthetic captures can also use `--input` as described in [PC.md](PC.md). The existing
`build/validation/norune.input` toggles R2; omit that toggle for a normal third-person capture.

## Remaining integration gates

1. Qualify a real Quest/Air Link session with Dylan: Vulkan negotiation, image orientation,
   stereo, tracking, recenter, focus/tracking loss and exit. Software receipts are separate.
2. Test third-person Norune while following Toan, orbiting/zooming and recovering at walls.
   Choose the rig's pitch/rotation/easing policy and game scale from actual headset feedback.
   Existing desktop camera/collision tests do not qualify headset comfort.
3. Measure frame times and deadline overruns, then optimize the serial eye transfers and small
   source resolution. Keep canonical game effects once per logic tick at the fixed 60 Hz cadence.
4. Give water, shadows, grabs and temporal feedback independent eye targets/history before
   enabling them. The bounded world-only prototype currently excludes these passes.
5. Implement readable HUD and Quest action bindings. Handle physical head/wall overlap, world
   sprites/markers and view-dependent lighting/billboards explicitly.
6. Extend world-camera selection/culling and transitions for dungeons, lock-on, first-person,
   loading/cuts, menus, talk/events, fishing and Georama. Those modes are not offered by this slice.

Relevant specification entries:
[Vulkan enable2](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_vulkan_enable2.html),
[runtime GPU selection](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetVulkanGraphicsDevice2KHR.html),
[device creation](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateVulkanDeviceKHR.html),
[frame timing](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html),
[frame submission](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndFrame.html).

## Current bridge and next qualification

The third-person Norune bridge now implements the former next-software plan. Startup negotiates
Vulkan through OpenXR before renderer creation; session/swapchains close before renderer teardown.
Only `xrWaitFrame` runs on a worker. There is at most one outstanding wait; game-thread presentation
polls it without blocking on headset pacing. Pose location, game recording and all Vulkan work remain
on the game thread. STOPPING and destruction join that wait before ending/destroying the session.
Rendering and image/GPU waits can still overrun a logic deadline; this prototype does not promise
that all graphics work fits inside a 60 Hz tick. Runtime-driven timing needs headset measurements.

The existing game remains at 60 Hz. Each tick records/renders canonically once; the host receives
its newest/previous display lists for interpolation. Predicted eye/head pose is sampled after the
wait completes and applied directly after the game camera. Separate runtime eye images receive
serial display renders. No game update, canonical upload or canonical depth query runs per eye.
The explicit Norune world-camera tag replaces the synthetic screenshot tool's camera-0 assumption.

The normal Norune walking gate bypasses desktop-frustum clipping in `MGClipVertex` and frame
hierarchy drawing, and expands the ground's four area flags/clip plane before recording. Other
modes and normal desktop play retain their culling. Eye replay requires a cleared, independent
canonical frame; loading, cuts, feedback/base-dependent frames and unsupported cameras produce
zero game layers. Eye-dependent water, shadows and screen-space effects are excluded. Shared game
history is therefore not being offered as support for those effects. Focus loss suppresses pad,
mouse look/zoom, keyboard movement and menu input without destroying device overrides; accumulated
mouse motion is dropped when focus returns.

Remaining qualification starts with Dylan's actual Air Link session, the calibration room, then
stationary and moving third-person Norune. Assess the game rig's automatic rotations, pitch,
follow easing, orbit/zoom, wall recovery, framing and scale before choosing a comfort policy.
Constrain physical head/wall overlap and place readable HUD later. Implement action bindings for
Quest thumbsticks/buttons after the first rendered session works. Isolate eye targets/history
before restoring water/shadows/feedback; replace the current serial GPU waits and small eye source
only after measuring them. Dungeon culling, lock-on, camera cuts and other game modes remain
explicit later work. Physical swings, hands and a combat redesign remain later scope.

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

Rebased both implementation commits onto upstream `master` at
`eded8d9b0a7488f0af0c90ce54d69fcc646c1052` (localized texture text and additional controller glyphs).
The rebase had no source conflicts and `git range-diff` reports both patches unchanged. The
implementation head became `f1e3805aed2832730075bfac34e728492226982b`; the pre-rebase head remains
available locally as `dcvr-before-master-20261008-22bfd6b9`. Both Windows builds succeeded.
The enabled suite had 309 passes and one desktop-focus skip out of 310 cases; the disabled suite
passed all 291. The skipped case could not obtain window input focus and passed in the disabled
build. All 19 DCVR cases passed. The copied-save Norune canonical image retained the exact baseline
hash above, and both Norune stereo eyes and both synthetic calibration eyes are byte-identical
to their pre-rebase captures. The fresh live probe still finds Oculus 1.208.0 and returns
`XR_ERROR_FORM_FACTOR_UNAVAILABLE` before device/session creation. Dylan is charging his Quest;
pairing and real headset validation remain pending. Rebase receipts are under
`build/validation/rebase-*-tests.log`, `norune-rebase*`, `calibration-rebase*` and `vr-rebase/`.

Dylan subsequently clarified that third-person play is fundamental and its camera is on the
critical path. The next-slice plan and integration gates above now require it; the earlier
first-person-first recommendation is superseded. A new copied-save Norune capture ran for 260
ticks with a neutral input script (no R2/first-person toggle), at 960x720. The canonical image and
synthetic eye image were visually inspected with Toan visible in third person, and the left/right
PNGs are different. The wider synthetic projection changes character framing, reinforcing the
need to qualify third-person camera distance and scale. This is stationary synthetic evidence,
not live tracking, follow/orbit qualification or headset validation. No renderer/gameplay code
changed for this correction. Receipts are `build/validation/norune-third-person*`.

The third implementation checkpoint adds `dcvr_game`, its presentation hook, nonblocking XR wait
stage, explicit Norune world-camera recording and bounded conservative culling. The normal follow
camera and conventional controls remain; recentered head/eye poses compose at display time.
Clears/cuts/base images and previous-frame feedback are gated, unsupported modes submit zero game
layers, and eye-dependent water/shadows/DOF plus screen-space UI/composites are excluded. Focus
loss suppresses all gameplay input channels and drops accumulated mouse movement. Normal desktop
builds do not link OpenXR.

Both Windows builds succeeded. **319 OpenXR-enabled tests and 297 disabled-build tests passed**,
with no skips. New cases cover delayed asynchronous waits and one outstanding frame, STOPPING
join order, unsupported empty frames, world-camera tagging, world-only replay/canonical HUD,
fixed canonical counts at simulated 72/90/120 Hz, focus gating, bounded Norune mode/culling and
rejection of unsafe feedback. These use injected XR calls and real Vulkan; they are not a live session.

The final copied-save movement run used `tools/dcvr/norune-walk.input`: 300 canonical/game ticks,
885 synthetic display frames and 1,776 eye renders including six final captures. Toan walked
159.9998 game units; the base camera changed 179 times. Neutral synthetic head motion left Toan
stationary. Repeating movement at 10 and 15 game units/metre produced identical avatar positions.
Final neutral/side/back eye images were inspected with Toan in third person and geometry beyond
the original camera visible. Left/right images differ. The ordinary desktop regression PNG still
matches SHA-256 `17DD3AEC6DDE882044F42D066240B39A32B3F1FDF1F60A09E5D6B4F98BB9FB15` exactly.
Existing capture directories were refused without changing their metadata, invalid scale was
refused, and unavailable-headset startup returned exit 1 with a structured failure receipt.

Receipts include `build/validation/game-all-tests-final.log`, `game-desktop-tests-final.log`,
`vr-game-desktop-regression*`, `vr-game-unavailable.log`, and fresh runs under
`build/dcvr/vr-tests/`. The launchers were exercised in synthetic mode and with an unavailable
headset. The latest live probe still finds Oculus 1.208.0 but reports
`XR_ERROR_FORM_FACTOR_UNAVAILABLE` before device/session creation. Air Link pairing, actual stereo,
tracking, camera comfort, world scale and headset performance remain for Dylan to test. No real
save, play installation, shortcut, active-runtime registration or matching `ps2/` source changed.
