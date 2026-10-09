#include "stereo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numbers>

#include "displaylist.hpp"

namespace gfx {

bool StereoWorldReplaySafe(const DisplayList &list) {
    if (list.world_camera < 0 || uint32_t(list.world_camera) >= list.cameras.size() ||
        !list.main_cleared || list.needs_base || list.cut || list.camera_cut) {
        return false;
    }
    bool world = std::any_of(list.records.begin(), list.records.end(), [&](const auto &record) {
        return record.has_transform && record.camera == uint32_t(list.world_camera);
    });
    if (!world) return false;
    for (const auto &entry : list.entries) {
        if (const auto *copy = std::get_if<detail::CopyEntry>(&entry)) {
            if (copy->src == kPreviousFrame) return false;
        } else if (const auto *draw = std::get_if<detail::MeshEntry>(&entry)) {
            if (draw->binding.texture == kPreviousFrame) return false;
        } else if (const auto *sprite = std::get_if<detail::Draw2DEntry>(&entry)) {
            if (sprite->binding.texture == kPreviousFrame) return false;
        }
    }
    return true;
}

bool MakeStereoView(const StereoPose &pose, const StereoFov &fov, float units_per_metre,
                    float near_z, float far_z, uint32_t camera, ViewOverride &out) {
    constexpr float half_pi = std::numbers::pi_v<float> / 2;
    for (float angle : {fov.left, fov.right, fov.up, fov.down}) {
        if (!std::isfinite(angle) || std::abs(angle) >= half_pi) {
            return false;
        }
    }
    if (!(fov.left < fov.right && fov.down < fov.up) ||
        !std::isfinite(units_per_metre) || !(units_per_metre > 0) ||
        !std::isfinite(near_z) || !std::isfinite(far_z) || !(near_z > 0 && far_z > near_z)) {
        return false;
    }
    float norm = 0;
    for (float component : pose.orientation) {
        if (!std::isfinite(component)) {
            return false;
        }
        norm += component * component;
    }
    // Runtime orientations must be unit quaternions. Tolerate ordinary float roundoff only.
    if (!std::isfinite(norm) || std::abs(norm - 1) > 0.01f) {
        return false;
    }
    for (float component : pose.position) {
        if (!std::isfinite(component) || !std::isfinite(component * units_per_metre)) {
            return false;
        }
    }
    const float inv_length = 1 / std::sqrt(norm);
    const float x = pose.orientation[0] * inv_length, y = -pose.orientation[1] * inv_length;
    const float z = -pose.orientation[2] * inv_length, w = pose.orientation[3] * inv_length;
    const float place[16] = {
        1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y), 0,
        2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x), 0,
        2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y), 0,
        pose.position[0] * units_per_metre, -pose.position[1] * units_per_metre,
        -pose.position[2] * units_per_metre, 1};
    ViewOverride result;
    result.camera = camera;
    if (!InvertAffineTransform(place, result.view_from_camera)) {
        return false;
    }
    for (float component : result.view_from_camera) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    const float l = std::tan(fov.left), r = std::tan(fov.right);
    const float u = std::tan(fov.up), d = std::tan(fov.down);
    result.projection[0] = 2 / (r - l);
    result.projection[5] = 2 / (u - d);
    result.projection[8] = -(r + l) / (r - l);
    result.projection[9] = (u + d) / (u - d);
    result.projection[10] = -near_z / (far_z - near_z);
    result.projection[11] = 1;
    result.projection[14] = near_z * (far_z / (far_z - near_z));
    for (float component : result.projection) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    out = result;
    return true;
}

bool WriteStereoCapture(const DisplayList &list, const std::filesystem::path &directory) {
    if (!ActiveRendererFeatures().offscreen || list.cameras.empty() || list.needs_base) {
        std::fprintf(stderr, "DCVR capture needs an offscreen, cleared 3D canonical frame\n");
        return false;
    }
    std::error_code error;
    // A fresh directory prevents mixing a failed capture with old eye images.
    if (!std::filesystem::create_directory(directory, error) || error) {
        std::fprintf(stderr, "DCVR capture needs a new directory with an existing parent\n");
        return false;
    }
    constexpr float angle = std::numbers::pi_v<float> / 4;
    for (int eye = 0; eye < 2; ++eye) {
        StereoPose pose;
        pose.position[0] = eye == 0 ? -0.032f : 0.032f;
        ViewOverride view;
        if (!MakeStereoView(pose, {-angle, angle, angle, -angle}, 10, 0.5f, 65535, 0, view) ||
            !RenderList(list, 1, {.present = true, .view_override = &view})) {
            return false;
        }
        std::vector<uint8_t> pixels;
        uint32_t             width = 0, height = 0;
        if (!ReadbackFrame(pixels, width, height) ||
            !WritePng(directory / (eye == 0 ? "left.png" : "right.png"), pixels.data(), width, height)) {
            return false;
        }
    }
    if (!PresentCanonical()) {
        return false;
    }
    std::ofstream manifest(directory / "capture.json");
    manifest << "{\n  \"kind\": \"synthetic_stereo_capture\",\n  \"headset_validated\": false,\n"
                "  \"camera\": 0,\n  \"ipd_metres\": 0.064,\n  \"units_per_metre\": 10,\n"
                "  \"fov_degrees_per_axis\": 90,\n  \"near_game_units\": 0.5,\n"
                "  \"far_game_units\": 65535\n}\n";
    manifest.close();
    if (manifest.fail()) {
        return false;
    }
    std::fprintf(stderr, "DCVR: captured synthetic left/right views; no headset was used\n");
    return true;
}

} // namespace gfx
