#pragma once

#include "common.hpp"

#if defined(XR_USE_GRAPHICS_API_VULKAN)
#include <openxr/openxr_platform.h>
#endif

#include <array>
#include <functional>
#include <vector>

namespace dcvr {

// Injectable only at the OpenXR boundary: deterministic tests exercise the real frame loop.
struct FrameApi {
    PFN_xrPollEvent             poll_event = xrPollEvent;
    PFN_xrBeginSession          begin_session = xrBeginSession;
    PFN_xrEndSession            end_session = xrEndSession;
    PFN_xrRequestExitSession    request_exit = xrRequestExitSession;
    PFN_xrWaitFrame             wait_frame = xrWaitFrame;
    PFN_xrBeginFrame            begin_frame = xrBeginFrame;
    PFN_xrEndFrame              end_frame = xrEndFrame;
    PFN_xrLocateViews           locate_views = xrLocateViews;
    PFN_xrLocateSpace           locate_space = xrLocateSpace;
    PFN_xrAcquireSwapchainImage acquire = xrAcquireSwapchainImage;
    PFN_xrWaitSwapchainImage    wait_image = xrWaitSwapchainImage;
    PFN_xrReleaseSwapchainImage release = xrReleaseSwapchainImage;
};

struct EyeSwapchain {
    XrSwapchain                            handle = XR_NULL_HANDLE;
    uint32_t                               width = 0, height = 0;
    VkFormat                               format = VK_FORMAT_UNDEFINED;
    std::vector<XrSwapchainImageVulkanKHR> images;
};

struct FrameViews {
    XrTime                time = 0;
    std::array<XrView, 2> eyes;
    XrPosef               head;
    // The LOCAL origin changed at this frame's predicted display time; discard a cached anchor.
    bool reference_changed = false;
};

enum class FrameResult {
    Idle,
    Empty,
    Rendered,
    Exit
};

// Single-threaded session state and frame ordering, independent of gameplay. Owns no XR/Vulkan
// handles. Poll events before each Frame. Destroy the owning session before its graphics device.
class FrameLoop {
public:
    using RenderEye = std::function<void(uint32_t eye, const FrameViews &, const EyeSwapchain &, uint32_t image)>;
    // Called before releasing a waited image even if RenderEye throws. Must finish GPU work and
    // restore the OpenXR image layout on failures too. It must not throw.
    using FinishGpu = std::function<void()>;

    FrameLoop(XrInstance instance, XrSession session, XrSpace local, XrSpace head,
              const std::array<EyeSwapchain, 2> &eyes, FrameApi api = {});
    void        Poll();
    FrameResult Frame(const RenderEye &render, const FinishGpu &finish);
    void        RequestExit();

    bool Running() const { return running_; }

    bool Focused() const { return state_ == XR_SESSION_STATE_FOCUSED; }

    bool Exiting() const { return exiting_; }

    XrSessionState State() const { return state_; }

    uint64_t Frames() const { return frames_; }

    uint64_t Submitted() const { return submitted_; }

private:
    void                               Observe(XrResult result, const char *call);
    XrInstance                         instance_;
    XrSession                          session_;
    XrSpace                            local_, head_;
    const std::array<EyeSwapchain, 2> &eyes_;
    FrameApi                           api_;
    XrSessionState                     state_ = XR_SESSION_STATE_UNKNOWN;
    bool                               running_ = false, exiting_ = false, exit_requested_ = false;
    std::vector<XrTime>                reference_changes_;
    uint64_t                           frames_ = 0, submitted_ = 0;
};

} // namespace dcvr
