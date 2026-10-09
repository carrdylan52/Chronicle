#pragma once

#include "gfx.hpp"

namespace gfx {

// OpenXR conventions, without exposing SDK types to the game. Angles are radians; the pose is
// eye-in-camera-reference, right handed, +Y up, -Z forward, metres. A future live session must
// supply a recentered, predicted pose, not an absolute room position.
struct StereoFov {
    float left, right, up, down;
};

struct StereoPose {
    float orientation[4] = {0, 0, 0, 1}; // x, y, z, w
    float position[3] = {};
};

// Converts the pose into the game's camera basis (+Y down, +Z forward) and produces an
// asymmetric Vulkan reverse-Z projection. Leaves out unchanged on invalid input.
bool MakeStereoView(const StereoPose &pose, const StereoFov &fov, float units_per_metre,
                    float near_z, float far_z, uint32_t camera, ViewOverride &out);

// The bounded world-only bridge cannot use canonical/previous-eye feedback as stereo history.
// Requires an explicit world camera, world draws and an independently cleared, uncut frame.
bool StereoWorldReplaySafe(const DisplayList &list);

// Development capture only: two sequential display replays of the latest canonical list.
// Does not advance simulation. Uses the first recorded camera, fixed 64 mm IPD, provisional
// 10 game units/metre and a symmetric 90-degree FOV. Requires an offscreen renderer and a
// cleared frame; not a headset session or a solution for per-eye temporal effects.
bool WriteStereoCapture(const DisplayList &list, const std::filesystem::path &directory);

} // namespace gfx
