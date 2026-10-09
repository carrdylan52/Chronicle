#include "frame_loop.hpp"

#include <algorithm>
#include <cstdio>

namespace dcvr {

FrameLoop::FrameLoop(XrInstance instance, XrSession session, XrSpace local, XrSpace head,
                     const std::array<EyeSwapchain, 2> &eyes, FrameApi api)
    : instance_(instance), session_(session), local_(local), head_(head), eyes_(eyes), api_(api) {}

void FrameLoop::Observe(XrResult result, const char *call) {
    if (XR_FAILED(result) || result == XR_SESSION_LOSS_PENDING) {
        exiting_ = true;
    }
    Check(result, call);
}

void FrameLoop::Poll() {
    if (exiting_) {
        return;
    }
    for (;;) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult          result = api_.poll_event(instance_, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return;
        }
        Observe(result, "poll_event");
        if (exiting_) {
            return;
        }
        if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            exiting_ = true;
        } else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const auto &change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending &>(event);
            if (change.session == session_ && change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                reference_changes_.push_back(change.changeTime);
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto &change = reinterpret_cast<const XrEventDataSessionStateChanged &>(event);
            if (change.session != session_) {
                continue;
            }
            state_ = change.state;
            std::fprintf(stderr, "DCVR: session state %d\n", int(state_));
            if (state_ == XR_SESSION_STATE_READY && !running_) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                Observe(api_.begin_session(session_, &begin), "begin_session");
                running_ = true;
            } else if (state_ == XR_SESSION_STATE_STOPPING && running_) {
                Observe(api_.end_session(session_), "end_session");
                running_ = false;
                if (exit_requested_) {
                    exiting_ = true;
                }
            } else if (state_ == XR_SESSION_STATE_EXITING || state_ == XR_SESSION_STATE_LOSS_PENDING) {
                running_ = false;
                exiting_ = true;
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
            // Session transitions may have been lost. Stop rather than guessing call order.
            exiting_ = true;
            throw Failure("events_lost", 0, "OpenXR lost events; restart the calibration runner");
        }
        if (exiting_) {
            return;
        }
    }
}

void FrameLoop::RequestExit() {
    if (exit_requested_ || exiting_) {
        return;
    }
    exit_requested_ = true;
    if (running_) {
        Observe(api_.request_exit(session_), "request_exit_session");
    } else {
        exiting_ = true;
    }
}

FrameResult FrameLoop::Frame(const RenderEye &render, const FinishGpu &finish) {
    if (exiting_) {
        return FrameResult::Exit;
    }
    if (!running_) {
        return FrameResult::Idle;
    }
    XrFrameState    state{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    Observe(api_.wait_frame(session_, &wait, &state), "wait_frame");
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    XrResult         begun = api_.begin_frame(session_, &begin);
    Observe(begun, "begin_frame");
    ++frames_;
    bool frame_open = true;
    auto end = [&](const XrCompositionLayerProjection *layer) {
        const auto    *header = reinterpret_cast<const XrCompositionLayerBaseHeader *>(layer);
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};
        info.displayTime = state.predictedDisplayTime;
        info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        info.layerCount = layer ? 1 : 0;
        info.layers = layer ? &header : nullptr;
        frame_open = false; // Never retry a failed xrEndFrame.
        Observe(api_.end_frame(session_, &info), "end_frame");
    };
    try {
        if (!state.shouldRender || exiting_ || begun == XR_FRAME_DISCARDED || exit_requested_) {
            end(nullptr);
            return FrameResult::Empty;
        }
        FrameViews views{};
        views.time = state.predictedDisplayTime;
        for (XrView &view : views.eyes) {
            view.type = XR_TYPE_VIEW;
        }
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = views.time;
        locate.space = local_;
        XrViewState located{XR_TYPE_VIEW_STATE};
        uint32_t    count = 0;
        Observe(api_.locate_views(session_, &locate, &located, 2, &count, views.eyes.data()), "locate_views");
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        Observe(api_.locate_space(head_, local_, views.time, &head), "locate_head");
        constexpr auto view_valid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        constexpr auto space_valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (exiting_ || count != 2 || (located.viewStateFlags & view_valid) != view_valid ||
            (head.locationFlags & space_valid) != space_valid) {
            end(nullptr);
            return FrameResult::Empty;
        }
        views.head = head.pose;
        std::erase_if(reference_changes_, [&](XrTime time) {
            if (time <= views.time) {
                views.reference_changed = true;
                return true;
            }
            return false;
        });
        std::array<XrCompositionLayerProjectionView, 2> projection{};
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const EyeSwapchain         &swapchain = eyes_[eye];
            uint32_t                    image = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            Observe(api_.acquire(swapchain.handle, &acquire, &image), "acquire_eye_image");
            XrSwapchainImageWaitInfo image_wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            image_wait.timeout = XR_INFINITE_DURATION;
            XrResult waited = api_.wait_image(swapchain.handle, &image_wait);
            Observe(waited, "wait_eye_image");
            if (waited == XR_TIMEOUT_EXPIRED) {
                // Not writable/releasable. Fatal for this session; its owner destroys the swapchain.
                throw Failure("wait_eye_image", int(waited), "Infinite swapchain wait unexpectedly timed out");
            }
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            bool                        release_attempted = false;
            try {
                if (image >= swapchain.images.size()) {
                    throw Failure("eye_image_index", 0, "Runtime returned an out-of-range swapchain image index");
                }
                if (!exiting_) {
                    render(eye, views, swapchain, image);
                }
                finish();
                release_attempted = true;
                Observe(api_.release(swapchain.handle, &release), "release_eye_image");
            } catch (...) {
                if (!release_attempted) {
                    finish();
                    // Best-effort unwind: release only images that have actually been waited.
                    api_.release(swapchain.handle, &release);
                }
                throw;
            }
            projection[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection[eye].pose = views.eyes[eye].pose;
            projection[eye].fov = views.eyes[eye].fov;
            projection[eye].subImage.swapchain = swapchain.handle;
            projection[eye].subImage.imageRect.extent = {int32_t(swapchain.width), int32_t(swapchain.height)};
            if (exiting_) {
                end(nullptr);
                return FrameResult::Empty;
            }
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = local_;
        layer.viewCount = 2;
        layer.views = projection.data();
        end(&layer);
        ++submitted_;
        return FrameResult::Rendered;
    } catch (...) {
        // After a rendering/API failure, recreate the owning session before doing more work.
        exiting_ = true;
        if (frame_open) {
            // Close an already begun frame with zero layers, preserving the original failure.
            try {
                end(nullptr);
            } catch (...) {
            }
        }
        throw;
    }
}

} // namespace dcvr
