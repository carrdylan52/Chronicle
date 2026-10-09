#pragma once

#include "frame_loop.hpp"
#include "gfx/stereo.hpp"

namespace dcvr {

// Calibration-only camera. One metre equals one renderer unit. Recenter removes position and
// yaw, preserving live pitch/roll. Virtual movement uses a body heading independent of head
// orientation; it never alters the raw LOCAL poses submitted to the compositor.
class RoomCamera {
public:
    void Recenter() { centered_ = false; }

    void            Center(const FrameViews &frame);
    void            Move(float forward, float right, float seconds);
    void            Turn(float radians);
    gfx::StereoPose Pose(const XrPosef &eye) const;

private:
    bool       centered_ = false;
    XrVector3f origin_{};
    float      origin_yaw_ = 0, body_yaw_ = 0;
    XrVector3f body_{};
};

gfx::DisplayListRef RecordRoom();
void                RenderRoomEye(const gfx::DisplayList &room, const RoomCamera &camera, const XrView &eye);

} // namespace dcvr
