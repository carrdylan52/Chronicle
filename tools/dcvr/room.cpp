#include "room.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace dcvr {
namespace {
XrQuaternionf Multiply(XrQuaternionf a, XrQuaternionf b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

void Quad(const std::array<std::array<float, 3>, 4> &points, const std::array<float, 3> &color,
          const gfx::MeshTransform &transform) {
    std::array<gfx::Vertex3D, 4> vertices{};
    for (size_t i = 0; i < points.size(); ++i) {
        std::copy(points[i].begin(), points[i].end(), vertices[i].position);
    }
    gfx::MeshConstants constants{};
    std::copy_n(transform.projection, 16, constants.mvp);
    std::copy(color.begin(), color.end(), constants.diffuse);
    constants.diffuse[3] = 1;
    gfx::DrawState state;
    state.depth_test = gfx::DepthTest::GEqual;
    state.depth_write = true;
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    gfx::DrawMeshImmediate(vertices, indices, constants, {}, state, &transform);
}

void Box(float x, float y, float z, float size, const std::array<float, 3> &color,
         const gfx::MeshTransform &transform) {
    const float l = x - size / 2, r = x + size / 2, t = y - size / 2, b = y + size / 2;
    const float n = z - size / 2, f = z + size / 2;
    Quad({
             {{l, t, n}, {r, t, n}, {r, b, n}, {l, b, n}}
    },
         color, transform);
    Quad({
             {{l, t, f}, {r, t, f}, {r, b, f}, {l, b, f}}
    },
         color, transform);
    auto side = color;
    for (float &c : side) {
        c *= .7f;
    }
    Quad({
             {{l, t, n}, {l, t, f}, {l, b, f}, {l, b, n}}
    },
         side, transform);
    Quad({
             {{r, t, n}, {r, t, f}, {r, b, f}, {r, b, n}}
    },
         side, transform);
    for (float &c : side) {
        c *= .8f;
    }
    Quad({
             {{l, t, n}, {r, t, n}, {r, t, f}, {l, t, f}}
    },
         side, transform);
    Quad({
             {{l, b, n}, {r, b, n}, {r, b, f}, {l, b, f}}
    },
         side, transform);
}
} // namespace

void RoomCamera::Center(const FrameViews &frame) {
    if (!centered_ || frame.reference_changed) {
        origin_ = frame.head.position;
        const auto &q = frame.head.orientation;
        origin_yaw_ = std::atan2(2 * (q.x * q.z + q.w * q.y), 1 - 2 * (q.x * q.x + q.y * q.y));
        centered_ = true;
    }
}

void RoomCamera::Move(float forward, float right, float seconds) {
    // No jump after a pause, focus loss or debugger stop. Speed is metres/second, not per frame.
    seconds = std::clamp(seconds, 0.0f, .05f);
    float length = std::hypot(forward, right);
    if (length > 1) {
        forward /= length;
        right /= length;
    }
    float distance = seconds * 1.0f;
    body_.x += (right * std::cos(body_yaw_) - forward * std::sin(body_yaw_)) * distance;
    body_.z += (-forward * std::cos(body_yaw_) - right * std::sin(body_yaw_)) * distance;
    body_.x = std::clamp(body_.x, -2.4f, 2.4f);
    body_.z = std::clamp(body_.z, -3.4f, 1.4f);
}

void RoomCamera::Turn(float radians) {
    body_yaw_ = std::remainder(body_yaw_ + radians, 2 * std::numbers::pi_v<float>);
}

gfx::StereoPose RoomCamera::Pose(const XrPosef &eye) const {
    float         yaw = body_yaw_ - origin_yaw_;
    XrQuaternionf rotation{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
    auto          q = Multiply(rotation, eye.orientation);
    float         x = eye.position.x - origin_.x, y = eye.position.y - origin_.y, z = eye.position.z - origin_.z;
    return {
        {q.x, q.y, q.z, q.w},
        {body_.x + std::cos(yaw) * x + std::sin(yaw) * z, y,
         body_.z - std::sin(yaw) * x + std::cos(yaw) * z}
    };
}

gfx::DisplayListRef RecordRoom() {
    constexpr float   angle = std::numbers::pi_v<float> / 4;
    gfx::ViewOverride view;
    if (!gfx::MakeStereoView({}, {-angle, angle, angle, -angle}, 1, .05f, 30, 0, view)) {
        throw Failure("room_projection", 0, "Cannot build calibration projection");
    }
    auto transform = gfx::IdentityMeshTransform();
    std::copy_n(view.projection, 16, transform.projection);
    gfx::BeginRecording();
    const uint8_t black[] = {8, 10, 16, 128};
    gfx::Clear(true, black, true, 0);
    // 6 x 6 x 3 metre room; floor is 1.6 metres below the recentered eye.
    Quad({
             {{-3, 1.6f, -2}, {3, 1.6f, -2}, {3, 1.6f, 4}, {-3, 1.6f, 4}}
    },
         {.24f, .28f, .34f}, transform);
    Quad({
             {{-3, -1.4f, -2}, {3, -1.4f, -2}, {3, -1.4f, 4}, {-3, -1.4f, 4}}
    },
         {.18f, .20f, .24f}, transform);
    Quad({
             {{-3, -1.4f, 4}, {3, -1.4f, 4}, {3, 1.6f, 4}, {-3, 1.6f, 4}}
    },
         {.28f, .34f, .42f}, transform);
    Quad({
             {{-3, -1.4f, -2}, {3, -1.4f, -2}, {3, 1.6f, -2}, {-3, 1.6f, -2}}
    },
         {.32f, .26f, .36f}, transform);
    Quad({
             {{-3, -1.4f, -2}, {-3, -1.4f, 4}, {-3, 1.6f, 4}, {-3, 1.6f, -2}}
    },
         {.36f, .24f, .24f}, transform);
    Quad({
             {{3, -1.4f, -2}, {3, -1.4f, 4}, {3, 1.6f, 4}, {3, 1.6f, -2}}
    },
         {.24f, .30f, .40f}, transform);
    // One metre floor grid, offset slightly to prevent depth fighting.
    for (int i = -2; i <= 3; ++i) {
        float p = float(i), w = .01f;
        Quad({
                 {{p - w, 1.599f, -2}, {p + w, 1.599f, -2}, {p + w, 1.599f, 4}, {p - w, 1.599f, 4}}
        },
             {.7f, .7f, .7f}, transform);
        Quad({
                 {{-3, 1.599f, p - w}, {3, 1.599f, p - w}, {3, 1.599f, p + w}, {-3, 1.599f, p + w}}
        },
             {.7f, .7f, .7f}, transform);
    }
    // A half-metre red cube and a one-metre blue cube give nearby and distant stereo cues.
    Box(-.7f, 1.35f, 1.3f, .5f, {.95f, .2f, .15f}, transform);
    Box(.85f, 1.1f, 2.8f, 1.0f, {.15f, .4f, .95f}, transform);
    Box(-1.1f, -.15f, 3.0f, .25f, {.2f, .9f, .4f}, transform);
    return gfx::EndRecording();
}

void RenderRoomEye(const gfx::DisplayList &room, const RoomCamera &camera, const XrView &eye) {
    gfx::ViewOverride view;
    gfx::StereoFov    fov{eye.fov.angleLeft, eye.fov.angleRight, eye.fov.angleUp, eye.fov.angleDown};
    if (!gfx::MakeStereoView(camera.Pose(eye.pose), fov, 1, .05f, 30, 0, view) ||
        !gfx::RenderList(room, 1, {.present = true, .view_override = &view})) {
        throw Failure("render_room_eye", 0, "Invalid eye pose/FOV or failed display replay");
    }
}

} // namespace dcvr
