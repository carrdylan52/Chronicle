#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <atomic>
#include <future>
#include <numbers>
#include <thread>

#include "frame_loop.hpp"
#include "gfx/context.hpp"
#include "room.hpp"

namespace {
using namespace dcvr;
const auto instance = reinterpret_cast<XrInstance>(uintptr_t(1));
const auto session = reinterpret_cast<XrSession>(uintptr_t(2));
const auto local = reinterpret_cast<XrSpace>(uintptr_t(3));
const auto head_space = reinterpret_cast<XrSpace>(uintptr_t(4));

struct Fake {
    static Fake                  *active;
    std::deque<XrEventDataBuffer> events;
    std::vector<std::string>      trace;
    XrBool32                      should_render = XR_TRUE;
    XrViewStateFlags              valid_views = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    XrSpaceLocationFlags          valid_head = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    XrResult                      begin_result = XR_SUCCESS, wait_result = XR_SUCCESS, image_result = XR_SUCCESS;
    XrResult                      acquire_result = XR_SUCCESS, release_result = XR_SUCCESS, end_result = XR_SUCCESS;
    uint32_t                      view_count = 2, image_index = 1, layers = 99;
    XrTime                        time = 100;
    XrPosef                       head{
        {0, 0, 0, 1},
        {0, 1.6f, 0}
    };
    std::array<XrView, 2>                           views{};
    std::array<XrCompositionLayerProjectionView, 2> submitted{};

    Fake() {
        active = this;
        constexpr float angle = std::numbers::pi_v<float> / 4;
        for (int i = 0; i < 2; ++i) {
            views[i].type = XR_TYPE_VIEW;
            views[i].pose = head;
            views[i].pose.position.x = i ? .032f : -.032f;
            views[i].fov = {-angle, angle, angle, -angle};
        }
    }

    ~Fake() { active = nullptr; }

    std::string Eye(XrSwapchain s) { return s == reinterpret_cast<XrSwapchain>(uintptr_t(5)) ? "L" : "R"; }

    template <class Event>
    void EventData(const Event &event) {
        XrEventDataBuffer data{};
        std::memcpy(&data, &event, sizeof(event));
        events.push_back(data);
    }

    void State(XrSessionState state, XrSession target = session) {
        XrEventDataSessionStateChanged event{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
        event.session = target;
        event.state = state;
        EventData(event);
    }

    static XrResult XRAPI_CALL Poll(XrInstance, XrEventDataBuffer *data) {
        if (active->events.empty()) {
            return XR_EVENT_UNAVAILABLE;
        }
        *data = active->events.front();
        active->events.pop_front();
        return XR_SUCCESS;
    }

    static XrResult XRAPI_CALL BeginSession(XrSession s, const XrSessionBeginInfo *info) {
        EXPECT_EQ(s, session);
        EXPECT_EQ(info->primaryViewConfigurationType, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);
        active->trace.push_back("start");
        return XR_SUCCESS;
    }

    static XrResult XRAPI_CALL EndSession(XrSession) {
        active->trace.push_back("stop");
        return XR_SUCCESS;
    }

    static XrResult XRAPI_CALL RequestExit(XrSession) {
        active->trace.push_back("exit");
        return XR_SUCCESS;
    }

    static XrResult XRAPI_CALL WaitFrame(XrSession, const XrFrameWaitInfo *, XrFrameState *state) {
        active->trace.push_back("wait");
        state->predictedDisplayTime = active->time;
        state->predictedDisplayPeriod = 11;
        state->shouldRender = active->should_render;
        return active->wait_result;
    }

    static XrResult XRAPI_CALL BeginFrame(XrSession, const XrFrameBeginInfo *) {
        active->trace.push_back("begin");
        return active->begin_result;
    }

    static XrResult XRAPI_CALL EndFrame(XrSession, const XrFrameEndInfo *info) {
        active->trace.push_back("end");
        EXPECT_EQ(info->displayTime, active->time);
        EXPECT_EQ(info->environmentBlendMode, XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        active->layers = info->layerCount;
        if (info->layerCount) {
            EXPECT_EQ(info->layerCount, 1u);
            const auto &layer = *reinterpret_cast<const XrCompositionLayerProjection *>(info->layers[0]);
            EXPECT_EQ(layer.space, local);
            EXPECT_EQ(layer.viewCount, 2u);
            std::copy_n(layer.views, 2, active->submitted.begin());
        }
        return active->end_result;
    }

    static XrResult XRAPI_CALL LocateViews(XrSession, const XrViewLocateInfo *info, XrViewState *state,
                                           uint32_t capacity, uint32_t *count, XrView *views) {
        active->trace.push_back("views");
        EXPECT_EQ(info->displayTime, active->time);
        EXPECT_EQ(info->space, local);
        EXPECT_EQ(capacity, 2u);
        state->viewStateFlags = active->valid_views;
        *count = active->view_count;
        std::copy(active->views.begin(), active->views.end(), views);
        return XR_SUCCESS;
    }

    static XrResult XRAPI_CALL LocateSpace(XrSpace head, XrSpace base, XrTime time, XrSpaceLocation *location) {
        active->trace.push_back("head");
        EXPECT_EQ(head, head_space);
        EXPECT_EQ(base, local);
        EXPECT_EQ(time, active->time);
        location->locationFlags = active->valid_head;
        location->pose = active->head;
        return XR_SUCCESS;
    }

    static XrResult XRAPI_CALL Acquire(XrSwapchain s, const XrSwapchainImageAcquireInfo *, uint32_t *index) {
        active->trace.push_back("acquire" + active->Eye(s));
        *index = active->image_index;
        return active->acquire_result;
    }

    static XrResult XRAPI_CALL WaitImage(XrSwapchain s, const XrSwapchainImageWaitInfo *info) {
        active->trace.push_back("wait" + active->Eye(s));
        EXPECT_EQ(info->timeout, XR_INFINITE_DURATION);
        return active->image_result;
    }

    static XrResult XRAPI_CALL Release(XrSwapchain s, const XrSwapchainImageReleaseInfo *) {
        active->trace.push_back("release" + active->Eye(s));
        return active->release_result;
    }

    FrameApi Api() {
        return {Poll, BeginSession, EndSession, RequestExit, WaitFrame, BeginFrame, EndFrame,
                LocateViews, LocateSpace, Acquire, WaitImage, Release};
    }
};

Fake *Fake::active = nullptr;

std::shared_future<void> wait_release;
std::atomic<bool> wait_entered;
XrResult XRAPI_CALL GatedWait(XrSession s, const XrFrameWaitInfo *info, XrFrameState *state) {
    wait_entered = true;
    wait_release.wait();
    return Fake::WaitFrame(s, info, state);
}

struct DCVRFrame : testing::Test {
    Fake                        fake;
    std::array<EyeSwapchain, 2> eyes;
    std::unique_ptr<FrameLoop>  loop;

    void SetUp() override {
        for (int i = 0; i < 2; ++i) {
            eyes[i].handle = reinterpret_cast<XrSwapchain>(uintptr_t(5 + i));
            eyes[i].width = 640 + i * 32;
            eyes[i].height = 480;
            eyes[i].images.resize(2, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        }
        loop = std::make_unique<FrameLoop>(instance, session, local, head_space, eyes, fake.Api());
    }

    void Ready() {
        fake.State(XR_SESSION_STATE_READY);
        fake.State(XR_SESSION_STATE_FOCUSED);
        loop->Poll();
        fake.trace.clear();
    }

    FrameLoop::RenderEye Draw() {
        return [&](uint32_t eye, const FrameViews &frame, const EyeSwapchain &chain, uint32_t image) {
            EXPECT_EQ(frame.time, fake.time);
            EXPECT_EQ(chain.handle, eyes[eye].handle);
            EXPECT_EQ(image, fake.image_index);
            fake.trace.push_back(eye ? "drawR" : "drawL");
        };
    }

    FrameLoop::FinishGpu Finish() {
        return [&] { fake.trace.push_back("finish"); };
    }
};

TEST_F(DCVRFrame, IdleReadyStopAndRestart) {
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Idle);
    EXPECT_TRUE(fake.trace.empty());
    fake.State(XR_SESSION_STATE_READY, reinterpret_cast<XrSession>(uintptr_t(90)));
    fake.State(XR_SESSION_STATE_READY);
    fake.State(XR_SESSION_STATE_READY);
    loop->Poll();
    EXPECT_EQ(fake.trace, std::vector<std::string>{"start"});
    fake.State(XR_SESSION_STATE_STOPPING);
    fake.State(XR_SESSION_STATE_READY);
    loop->Poll();
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"start", "stop", "start"}));
    EXPECT_TRUE(loop->Running());
    EXPECT_FALSE(loop->Focused());
}

TEST_F(DCVRFrame, StereoOrderingTimeAndRawCompositionPose) {
    Ready();
    ASSERT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Rendered);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "views", "head", "acquireL", "waitL", "drawL", "finish", "releaseL", "acquireR", "waitR", "drawR", "finish", "releaseR", "end"}));
    EXPECT_EQ(loop->Frames(), 1u);
    EXPECT_EQ(loop->Submitted(), 1u);
    EXPECT_EQ(fake.layers, 1u);
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(fake.submitted[i].subImage.swapchain, eyes[i].handle);
        EXPECT_EQ(fake.submitted[i].subImage.imageRect.extent.width, int32_t(eyes[i].width));
        EXPECT_FLOAT_EQ(fake.submitted[i].pose.position.x, fake.views[i].pose.position.x);
        EXPECT_FLOAT_EQ(fake.submitted[i].pose.position.y, 1.6f);
        EXPECT_FLOAT_EQ(fake.submitted[i].fov.angleLeft, fake.views[i].fov.angleLeft);
    }
}

TEST_F(DCVRFrame, HiddenAndDiscardedFramesEndWithoutImages) {
    Ready();
    fake.should_render = XR_FALSE;
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Empty);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "end"}));
    EXPECT_EQ(fake.layers, 0u);
    fake.trace.clear();
    fake.should_render = XR_TRUE;
    fake.begin_result = XR_FRAME_DISCARDED;
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Empty);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "end"}));
    EXPECT_EQ(loop->Submitted(), 0u);
}

TEST_F(DCVRFrame, UnsupportedGameFrameEndsWithoutLocatingOrAcquiring) {
    Ready();
    EXPECT_EQ(loop->Frame(Draw(), Finish(), false), FrameResult::Empty);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "end"}));
    EXPECT_EQ(fake.layers, 0u);
}

TEST_F(DCVRFrame, DelayedRuntimeWaitLeavesGameplayThreadAvailableAndQueuesOnlyOneFrame) {
    std::promise<void> release;
    wait_release = release.get_future().share();
    wait_entered = false;
    auto api = fake.Api();
    api.wait_frame = GatedWait;
    loop = std::make_unique<FrameLoop>(instance, session, local, head_space, eyes, api);
    loop->AsyncWait(true);
    Ready();
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Idle);
    // Simulated game logic keeps advancing while the runtime wait is deliberately blocked.
    for (int tick = 0; tick < 120; ++tick) {
        EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Idle);
        EXPECT_EQ(loop->Frames(), 0u);
    }
    release.set_value();
    auto result = FrameResult::Idle;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (result == FrameResult::Idle && std::chrono::steady_clock::now() < deadline) {
        result = loop->Frame(Draw(), Finish());
        std::this_thread::yield();
    }
    EXPECT_EQ(result, FrameResult::Rendered);
    EXPECT_EQ(loop->Frames(), 1u);
    EXPECT_EQ(loop->Submitted(), 1u);
    EXPECT_EQ(std::count(fake.trace.begin(), fake.trace.end(), "wait"), 1);
    EXPECT_EQ(fake.trace.back(), "end");
}

TEST_F(DCVRFrame, StoppingJoinsPendingWaitBeforeEndingSession) {
    std::promise<void> release;
    wait_release = release.get_future().share();
    wait_entered = false;
    auto api = fake.Api();
    api.wait_frame = GatedWait;
    loop = std::make_unique<FrameLoop>(instance, session, local, head_space, eyes, api);
    loop->AsyncWait(true);
    Ready();
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Idle);
    auto unblock = std::async(std::launch::async, [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        release.set_value();
    });
    fake.State(XR_SESSION_STATE_STOPPING);
    loop->Poll();
    unblock.get();
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "stop"}));
    EXPECT_FALSE(loop->Running());
}

TEST_F(DCVRFrame, InvalidTrackingAndViewCountSuppressDrawing) {
    Ready();
    for (int invalid = 0; invalid < 3; ++invalid) {
        fake.valid_views = invalid == 0 ? XR_VIEW_STATE_ORIENTATION_VALID_BIT : XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        fake.valid_head = invalid == 1 ? XR_SPACE_LOCATION_POSITION_VALID_BIT : XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        fake.view_count = invalid == 2 ? 1 : 2;
        fake.trace.clear();
        EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Empty);
        EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "views", "head", "end"}));
        EXPECT_EQ(fake.layers, 0u);
    }
}

TEST_F(DCVRFrame, RenderFailureFinishesGpuReleasesImageAndEndsEmpty) {
    Ready();
    EXPECT_THROW(loop->Frame([&](uint32_t eye, const FrameViews &, const EyeSwapchain &, uint32_t) {
        fake.trace.push_back(eye ? "drawR" : "drawL");
        if (eye == 1) {
            throw std::runtime_error("injected render failure");
        }
    },
                             Finish()),
                 std::runtime_error);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "views", "head", "acquireL", "waitL", "drawL", "finish", "releaseL", "acquireR", "waitR", "drawR", "finish", "releaseR", "end"}));
    EXPECT_EQ(fake.layers, 0u);
    EXPECT_EQ(loop->Submitted(), 0u);
}

TEST_F(DCVRFrame, FailedWaitNeverReleasesAnUnwaitedImage) {
    Ready();
    for (XrResult failure : {XR_ERROR_RUNTIME_FAILURE, XR_TIMEOUT_EXPIRED}) {
        loop = std::make_unique<FrameLoop>(instance, session, local, head_space, eyes, fake.Api());
        Ready();
        fake.image_result = failure;
        fake.trace.clear();
        EXPECT_THROW(loop->Frame(Draw(), Finish()), Failure);
        EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "views", "head", "acquireL", "waitL", "end"}));
        EXPECT_EQ(fake.layers, 0u);
        EXPECT_TRUE(loop->Exiting());
    }
}

TEST_F(DCVRFrame, FailedAcquireDoesNotWaitOrRelease) {
    Ready();
    fake.acquire_result = XR_ERROR_RUNTIME_FAILURE;
    EXPECT_THROW(loop->Frame(Draw(), Finish()), Failure);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "views", "head", "acquireL", "end"}));
    EXPECT_EQ(fake.layers, 0u);
}

TEST_F(DCVRFrame, ImageLossPendingIsReleasedWithoutDrawingOrSubmitting) {
    Ready();
    fake.image_result = XR_SESSION_LOSS_PENDING;
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Empty);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "views", "head", "acquireL", "waitL", "finish", "releaseL", "end"}));
    EXPECT_TRUE(loop->Exiting());
    EXPECT_EQ(fake.layers, 0u);
}

TEST_F(DCVRFrame, FailedReleaseAndEndAreNeverRetried) {
    Ready();
    fake.release_result = XR_ERROR_RUNTIME_FAILURE;
    EXPECT_THROW(loop->Frame(Draw(), Finish()), Failure);
    EXPECT_EQ(std::count(fake.trace.begin(), fake.trace.end(), "releaseL"), 1);
    EXPECT_EQ(fake.trace.back(), "end");
    EXPECT_EQ(fake.layers, 0u);
    fake.release_result = XR_SUCCESS;
    fake.end_result = XR_ERROR_RUNTIME_FAILURE;
    loop = std::make_unique<FrameLoop>(instance, session, local, head_space, eyes, fake.Api());
    Ready();
    EXPECT_THROW(loop->Frame(Draw(), Finish()), Failure);
    EXPECT_EQ(std::count(fake.trace.begin(), fake.trace.end(), "end"), 1);
    EXPECT_EQ(loop->Submitted(), 0u);
}

TEST_F(DCVRFrame, OutOfRangeImageIsReleasedAfterWaitWithoutDrawing) {
    Ready();
    fake.image_index = 3;
    EXPECT_THROW(loop->Frame(Draw(), Finish()), Failure);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "views", "head", "acquireL", "waitL", "finish", "releaseL", "end"}));
}

TEST_F(DCVRFrame, LossPendingCompletesBegunFrameAndStops) {
    Ready();
    fake.wait_result = XR_SESSION_LOSS_PENDING;
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Empty);
    EXPECT_EQ(fake.trace, (std::vector<std::string>{"wait", "begin", "end"}));
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Exit);
    EXPECT_TRUE(loop->Exiting());
    EXPECT_EQ(loop->Submitted(), 0u);
}

TEST_F(DCVRFrame, InstanceLossAndLostEventsStopWithoutWaiting) {
    Ready();
    XrEventDataInstanceLossPending loss{XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING};
    fake.EventData(loss);
    loop->Poll();
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Exit);
    EXPECT_TRUE(fake.trace.empty());
    loop = std::make_unique<FrameLoop>(instance, session, local, head_space, eyes, fake.Api());
    XrEventDataEventsLost lost{XR_TYPE_EVENT_DATA_EVENTS_LOST};
    lost.lostEventCount = 1;
    fake.EventData(lost);
    EXPECT_THROW(loop->Poll(), Failure);
}

TEST_F(DCVRFrame, ExitRequestedOnceAndEndsAtStopping) {
    Ready();
    loop->RequestExit();
    loop->RequestExit();
    EXPECT_EQ(fake.trace, std::vector<std::string>{"exit"});
    EXPECT_EQ(loop->Frame(Draw(), Finish()), FrameResult::Empty);
    EXPECT_EQ(fake.layers, 0u);
    fake.State(XR_SESSION_STATE_STOPPING);
    loop->Poll();
    EXPECT_TRUE(loop->Exiting());
    EXPECT_FALSE(loop->Running());
}

TEST_F(DCVRFrame, ReferenceChangeAppliesAtPredictedTimeAndOnlyOnce) {
    Ready();
    XrEventDataReferenceSpaceChangePending change{XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING};
    change.session = session;
    change.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    change.changeTime = 150;
    fake.EventData(change);
    loop->Poll();
    for (int time : {100, 150, 160}) {
        fake.time = time;
        EXPECT_EQ(loop->Frame([&](uint32_t, const FrameViews &frame, const EyeSwapchain &, uint32_t) {
            EXPECT_EQ(frame.reference_changed, time == 150);
        },
                              Finish()),
                  FrameResult::Rendered);
    }
}

TEST(DCVRMath, VulkanVersionIntersection) {
    EXPECT_EQ(VulkanVersion(XR_MAKE_VERSION(1, 0, 0), XR_MAKE_VERSION(1, 3, 0), VK_API_VERSION_1_4), VK_API_VERSION_1_3);
    EXPECT_EQ(VulkanVersion(XR_MAKE_VERSION(1, 4, 9), XR_MAKE_VERSION(1, 4, 0), VK_API_VERSION_1_4), VK_API_VERSION_1_4);
    EXPECT_THROW(VulkanVersion(XR_MAKE_VERSION(1, 0, 0), XR_MAKE_VERSION(1, 2, 99), VK_API_VERSION_1_4), Failure);
    EXPECT_THROW(VulkanVersion(XR_MAKE_VERSION(1, 4, 0), XR_MAKE_VERSION(1, 4, 0), VK_API_VERSION_1_3), Failure);
}

TEST(DCVRMath, RecenterPreservesIpdPitchLeanAndMovement) {
    RoomCamera camera;
    FrameViews frame{};
    frame.head.orientation.w = 1;
    frame.head.position = {2, 1.6f, 3};
    camera.Center(frame);
    XrPosef eye = frame.head;
    eye.position.x -= .032f;
    auto pose = camera.Pose(eye);
    EXPECT_NEAR(pose.position[0], -.032f, 1e-6f);
    EXPECT_NEAR(pose.position[1], 0, 1e-6f);
    eye.position.y += .2f;
    eye.position.z -= .15f;
    eye.orientation = {.2f, 0, 0, std::sqrt(.96f)};
    pose = camera.Pose(eye);
    EXPECT_NEAR(pose.position[1], .2f, 1e-6f);
    EXPECT_NEAR(pose.position[2], -.15f, 1e-6f);
    EXPECT_FLOAT_EQ(pose.orientation[0], .2f);
    camera.Move(1, 0, 1);
    pose = camera.Pose(eye);
    EXPECT_NEAR(pose.position[2], -.2f, 1e-6f);
    camera.Turn(std::numbers::pi_v<float> / 2);
    camera.Move(1, 0, .05f);
    pose = camera.Pose(eye);
    EXPECT_NEAR(pose.position[0], -.2f, 1e-6f);
    camera.Recenter();
    frame.head = eye;
    camera.Center(frame);
    pose = camera.Pose(eye);
    EXPECT_NEAR(pose.position[1], 0, 1e-6f); // Virtual body position survives recenter.
    EXPECT_NEAR(pose.position[0], -.05f, 1e-6f);
    EXPECT_NEAR(pose.position[2], -.05f, 1e-6f);
}

TEST(DCVRMath, YawRecenterDoesNotZeroPitchOrRotateStereoBaselineWrongly) {
    RoomCamera  camera;
    FrameViews  frame{};
    const float s = std::sqrt(.5f);
    frame.head.orientation = {0, s, 0, s};
    camera.Center(frame);
    XrPosef left = frame.head;
    left.position.z = .032f;
    auto pose = camera.Pose(left);
    EXPECT_NEAR(pose.position[0], -.032f, 1e-6f);
    EXPECT_NEAR(pose.position[2], 0, 1e-6f);
    EXPECT_NEAR(pose.orientation[1], 0, 1e-6f);
    EXPECT_NEAR(pose.orientation[3], 1, 1e-6f);
}

// Real Vulkan transfer path, with an injected OpenXR boundary: separate eye images stay
// different after both replays, while canonical colour remains unchanged. No HMD is involved.
TEST_F(DCVRFrame, RealGpuStereoTransferKeepsEyesAndCanonicalIndependent) {
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window = SDL_CreateWindow("DCVR GPU test", 640, 480, SDL_WINDOW_HIDDEN);
    ASSERT_NE(window, nullptr);
    gfx::RendererConfig config;
    config.offscreen = true;
    config.render_scale = 1;
    config.pipeline_cache.clear();
    gfx::RendererInit(window, config);
    auto room = RecordRoom();
    ASSERT_TRUE(gfx::RenderList(*room, 1, {.canonical = true}));
    std::vector<uint8_t> canonical, after;
    uint32_t             width = 0, height = 0;
    ASSERT_TRUE(gfx::ReadbackFrame(canonical, width, height));
    std::array<gfx::detail::Image, 2> images;
    for (int i = 0; i < 2; ++i) {
        images[i] = gfx::detail::CreateImage(640, 480, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        gfx::detail::RunOneShot([&](VkCommandBuffer cmd) {
            gfx::detail::Transition(cmd, images[i], {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT});
        });
        eyes[i].width = 640;
        eyes[i].height = 480;
        eyes[i].format = images[i].format;
        eyes[i].images[1].image = images[i].image;
    }
    Ready();
    RoomCamera                          camera;
    std::array<std::vector<uint8_t>, 2> expected;
    EXPECT_EQ(loop->Frame([&](uint32_t eye, const FrameViews &frame, const EyeSwapchain &chain, uint32_t index) {
        if (eye == 0) { camera.Center(frame); }
        RenderRoomEye(*room, camera, frame.eyes[eye]);
        EXPECT_TRUE(gfx::CopyDisplayToVulkanImage(chain.images[index].image, chain.format, chain.width, chain.height));
        EXPECT_TRUE(gfx::ReadbackFrame(expected[eye], width, height)); }, [&] { vkDeviceWaitIdle(gfx::ActiveVulkanContext().device); }), FrameResult::Rendered);
    std::array<std::vector<uint8_t>, 2> pixels;
    for (int i = 0; i < 2; ++i) {
        auto buffer = gfx::detail::CreateBuffer(640 * 480 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        gfx::detail::RunOneShot([&](VkCommandBuffer cmd) {
            gfx::detail::Transition(cmd, images[i], gfx::detail::TransferSrc());
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {640, 480, 1};
            vkCmdCopyImageToBuffer(cmd, images[i].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &region);
            VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dependency);
        });
        pixels[i].assign(buffer.memory.mapped, buffer.memory.mapped + 640 * 480 * 4);
        EXPECT_EQ(pixels[i], expected[i]);
        gfx::detail::DestroyBuffer(buffer);
        gfx::detail::DestroyImage(images[i]);
    }
    EXPECT_NE(pixels[0], pixels[1]);
    ASSERT_TRUE(gfx::PresentCanonical());
    ASSERT_TRUE(gfx::ReadbackFrame(after, width, height));
    EXPECT_EQ(after, canonical);
    room.reset();
    gfx::RendererShutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST(DCVRGraphics, RendererUsesProvidedVersionDeviceAndCreationHooks) {
    struct Provider : gfx::VulkanProvider {
        uint32_t         instances = 0, devices = 0, selections = 0, version = 0;
        VkPhysicalDevice selected = VK_NULL_HANDLE;

        uint32_t ApiVersion(uint32_t loader) const override {
            EXPECT_GE(loader, VK_API_VERSION_1_3);
            return VK_API_VERSION_1_3;
        }

        VkResult CreateInstance(const VkInstanceCreateInfo &info, VkInstance &value) override {
            ++instances;
            version = info.pApplicationInfo->apiVersion;
            return vkCreateInstance(&info, nullptr, &value);
        }

        VkPhysicalDevice PhysicalDevice(VkInstance value) override {
            ++selections;
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(value, &count, nullptr);
            std::vector<VkPhysicalDevice> physicals(count);
            vkEnumeratePhysicalDevices(value, &count, physicals.data());
            for (auto physical : physicals) {
                auto caps = gfx::detail::QueryDeviceCaps(physical, VK_NULL_HANDLE);
                if (gfx::detail::MissingRequirements(caps, true).empty() && caps.features13.shaderDemoteToHelperInvocation) {
                    selected = physical;
                    return selected;
                }
            }
            throw Failure("test_gpu", 0, "No compatible Vulkan GPU for provider test");
        }

        VkResult CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo &info, VkDevice &value) override {
            ++devices;
            EXPECT_EQ(physical, selected);
            return vkCreateDevice(physical, &info, nullptr, &value);
        }
    } provider;

    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("DCVR provider test", 640, 480, SDL_WINDOW_HIDDEN);
    ASSERT_NE(window, nullptr);
    gfx::RendererConfig config;
    config.offscreen = true;
    config.pipeline_cache.clear();
    config.vulkan_provider = &provider;
    gfx::RendererInit(window, config);
    EXPECT_EQ(provider.instances, 1u);
    EXPECT_EQ(provider.devices, 1u);
    EXPECT_EQ(provider.selections, 1u);
    EXPECT_EQ(provider.version, VK_API_VERSION_1_3);
    EXPECT_EQ(gfx::ActiveVulkanContext().physical_device, provider.selected);
    auto room = RecordRoom();
    EXPECT_TRUE(gfx::RenderList(*room, 1, {.canonical = true}));
    EXPECT_FALSE(gfx::CopyDisplayToVulkanImage(VK_NULL_HANDLE, VK_FORMAT_R8G8B8A8_UNORM, 640, 480));
    room.reset();
    gfx::RendererShutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
} // namespace
