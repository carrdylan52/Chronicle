#include "game_bridge.hpp"
#include "room.hpp"
#include "runtime.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <thread>

#include "gameloop.hpp"
#include "character.hpp"
#include "editloop.hpp"
#include "gfx/displaylist.hpp"
#include "platform/paths.hpp"
#include "platform/input.hpp"

namespace dcvr {
struct GameBridge::Impl {
    static Impl   *active;
    nlohmann::json report{
        {"schema",            1                    },
        {"kind",              "norune_third_person"},
        {"headset_validated", false                },
        {"runtime_changed",   false                }
    };
    Runtime                 runtime;
    RoomCamera              head;
    std::filesystem::path   synthetic;
    int                     seconds;
    float                   scale;
    SDL_Renderer           *controls = nullptr;
    SDL_Texture            *mirror = nullptr;
    bool                    started = false, stopped = false, failed = false, input_blocked = false;
    bool                    capture_created = false;
    bool                    scene_ready = false;
    uint64_t                eye_renders = 0, synthetic_frames = 0, mirror_frames = 0;
    uint64_t                sampled_camera_frames = 0, camera_changes = 0;
    std::array<float, 16>   last_camera{};
    std::array<float, 3>    first_avatar{}, last_avatar{};
    double                  walked_units = 0;
    const gfx::DisplayList *last_list = nullptr;
    using Clock = std::chrono::steady_clock;
    Clock::time_point start, next_ui, next_mirror;

    Impl(std::filesystem::path capture, int duration, float units)
        : synthetic(std::move(capture)), seconds(duration), scale(units) {}

    void FailureReport(const std::exception &failure) {
        failed = true;
        report["status"] = "failed";
        report["failure"] = failure.what();
        if (const auto *xr = dynamic_cast<const Failure *>(&failure)) {
            report["failure_stage"] = xr->stage;
            report["failure_code"] = xr->code;
        }
        std::fprintf(stderr, "DCVR game: %s\n", failure.what());
        GameRequestStop();
    }

    bool Eligible(const gfx::DisplayList &list) {
        return GameVrScene() && gfx::StereoWorldReplaySafe(list) && gfx::DisplayReplayReady();
    }

    void Eye(const gfx::DisplayList &list, const gfx::DisplayList *previous, float alpha,
             const XrView &eye) {
        gfx::ViewOverride    view;
        const gfx::StereoFov fov{eye.fov.angleLeft, eye.fov.angleRight, eye.fov.angleUp, eye.fov.angleDown};
        if (!gfx::MakeStereoView(head.Pose(eye.pose), fov, scale, .05f * scale, 65535,
                                 uint32_t(list.world_camera), view) ||
            !gfx::RenderList(list, alpha, {.previous = previous, .present = true, .view_override = &view, .world_only = true})) {
            throw Failure("render_game_eye", 0, "Invalid eye pose/FOV or unavailable game display replay");
        }
        ++eye_renders;
    }

    FrameViews SyntheticPose(float yaw = 0, float lean = 0) {
        FrameViews frame{};
        frame.head.orientation = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
        frame.head.position = {lean, 1.6f, 0};
        constexpr float angle = std::numbers::pi_v<float> / 4;
        for (int eye = 0; eye < 2; ++eye) {
            auto &view = frame.eyes[eye];
            view.type = XR_TYPE_VIEW;
            view.pose = frame.head;
            const float offset = eye ? .032f : -.032f;
            view.pose.position.x += std::cos(yaw) * offset;
            view.pose.position.z -= std::sin(yaw) * offset;
            view.fov = {-angle, angle, angle, -angle};
        }
        return frame;
    }

    void Mirror() {
        // Operator feedback only, at 2 Hz; never drives game or headset pacing.
        if (!controls || Clock::now() < next_mirror) {
            return;
        }
        next_mirror = Clock::now() + std::chrono::milliseconds(500);
        std::vector<uint8_t> pixels;
        uint32_t             width = 0, height = 0;
        if (!gfx::ReadbackFrame(pixels, width, height)) {
            return;
        }
        if (mirror) {
            SDL_DestroyTexture(mirror);
        }
        mirror = SDL_CreateTexture(controls, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                   int(width), int(height));
        if (mirror && SDL_UpdateTexture(mirror, nullptr, pixels.data(), int(width * 4))) {
            ++mirror_frames;
        }
    }

    void Present(const gfx::DisplayList &list, const gfx::DisplayList *previous, float alpha) {
        scene_ready = Eligible(list);
        if (scene_ready) {
            last_list = &list;
            if (alpha == 1) {
                report["world_camera_index"] = list.world_camera;
                report["recorded_camera_count"] = list.cameras.size();
                const auto &camera = list.cameras[list.world_camera];
                if (sampled_camera_frames && camera != last_camera) {
                    ++camera_changes;
                }
                last_camera = camera;
                ++sampled_camera_frames;
                if (Chara) {
                    float position[4];
                    Chara->GetPosition(position);
                    if (sampled_camera_frames == 1) {
                        std::copy_n(position, 3, first_avatar.begin());
                    } else {
                        walked_units += std::sqrt(std::pow(position[0] - last_avatar[0], 2) +
                                                  std::pow(position[2] - last_avatar[2], 2));
                    }
                    std::copy_n(position, 3, last_avatar.begin());
                }
            }
        } else {
            last_list = nullptr;
        }
        if (!synthetic.empty()) {
            if (!scene_ready) {
                return;
            }
            // Three display frames per canonical tick, each with a newly sampled synthetic
            // head pose. These are diagnostics, never an OpenXR/session/headset claim.
            for (int sample = 0; sample < 3; ++sample) {
                float phase = float(synthetic_frames++) * .03f;
                auto  frame = SyntheticPose(.25f * std::sin(phase), .10f * std::sin(phase * .7f));
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    Eye(list, previous, float(sample + 1) / 3, frame.eyes[eye]);
                }
            }
            return;
        }
        auto &loop = runtime.Loop();
        loop.Poll();
        if (loop.Exiting()) {
            GameRequestStop();
            return;
        }
        auto graphics = gfx::ActiveVulkanContext();
        loop.Frame([&](uint32_t eye, const FrameViews &frame, const EyeSwapchain &swapchain, uint32_t image) {
            if (eye == 0) {
                head.Center(frame);
            }
            Eye(list, previous, alpha, frame.eyes[eye]);
            if (!gfx::CopyDisplayToVulkanImage(swapchain.images[image].image, swapchain.format,
                                               swapchain.width, swapchain.height)) {
                throw Failure("copy_game_eye", 0, "Cannot transfer the game eye to its OpenXR image");
            }
            if (eye == 0) {
                Mirror();
            }
        }, [&] { vkDeviceWaitIdle(graphics.device); }, scene_ready);
    }

    static bool Hook(const gfx::DisplayList &list, const gfx::DisplayList *previous, float alpha,
                     bool between, Clock::time_point deadline) {
        if (!active || active->failed) {
            return false;
        }
        try {
            active->Present(list, previous, alpha);
        } catch (const std::exception &failure) {
            active->FailureReport(failure);
            return false;
        }
        if (between) {
            std::this_thread::sleep_until(std::min(deadline, Clock::now() + std::chrono::milliseconds(1)));
            return !GameStopRequested();
        }
        return false;
    }

    static void Event(const SDL_Event &event) {
        if (!active || event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
            return;
        }
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            GameRequestStop();
        }
        if (event.key.scancode == SDL_SCANCODE_F9) {
            active->head.Recenter();
        }
    }

    void Ui() {
        if (!controls || Clock::now() < next_ui) {
            return;
        }
        next_ui = Clock::now() + std::chrono::milliseconds(100);
        SDL_SetRenderDrawColor(controls, 18, 22, 32, 255);
        SDL_RenderClear(controls);
        int width, height;
        SDL_GetWindowSize(WindowHandle(), &width, &height);
        if (mirror && scene_ready) {
            SDL_FRect rect{0, 130, float(width), float(height - 130)};
            SDL_RenderTexture(controls, mirror, nullptr, &rect);
        }
        SDL_SetRenderDrawColor(controls, 235, 240, 248, 255);
        SDL_RenderDebugText(controls, 12, 12, "DCVR - third-person Norune prototype");
        SDL_RenderDebugText(controls, 12, 32, "WASD / gamepad: move Toan. Mouse / right stick: game camera.");
        SDL_RenderDebugText(controls, 12, 52, "F9: recenter head. Esc: exit. Keep this window focused.");
        SDL_RenderDebugText(controls, 12, 72, "World-only: avoid menus, first-person, talk and map exits.");
        SDL_RenderDebugText(controls, 12, 92, scene_ready ? "Norune world active. Mirror updates at 2 Hz." : "Loading / unsupported camera: no game layer submitted to headset.");
        SDL_RenderPresent(controls);
    }

    void Capture() {
        if (!last_list || !Eligible(*last_list)) {
            throw Failure("synthetic_game_capture", 0, "No eligible third-person Norune frame at exit");
        }

        struct View {
            const char *name;
            float       yaw, lean;
        } views[] = {
            {"neutral",    0,     0   },
            {"look-right", -.75f, .15f},
            {"look-back",  2.5f,  0   }
        };

        for (const auto &capture : views) {
            auto frame = SyntheticPose(capture.yaw, capture.lean);
            for (uint32_t eye = 0; eye < 2; ++eye) {
                Eye(*last_list, nullptr, 1, frame.eyes[eye]);
                std::vector<uint8_t> pixels;
                uint32_t             width = 0, height = 0;
                if (!gfx::ReadbackFrame(pixels, width, height) ||
                    !gfx::WritePng(synthetic / (std::string(capture.name) + (eye ? "-right.png" : "-left.png")),
                                   pixels.data(), width, height)) {
                    throw Failure("synthetic_game_capture", 0, "Cannot write game eye PNG");
                }
            }
        }
    }
};

GameBridge::Impl *GameBridge::Impl::active = nullptr;

GameBridge::GameBridge(std::filesystem::path synthetic, int seconds, float units)
    : impl_(std::make_unique<Impl>(std::move(synthetic), seconds, units)) {}

GameBridge::~GameBridge() {
    try {
        Stop();
    } catch (const std::exception &failure) {
        std::fprintf(stderr, "DCVR cleanup: %s\n", failure.what());
    }
}

void GameBridge::Configure(WindowConfig &window, gfx::RendererConfig &renderer) {
    auto &s = *impl_;
    window.vulkan = false;
    window.fullscreen = false;
    renderer.offscreen = true;
    renderer.render_scale = 1;
    renderer.layout.aspect = gfx::AspectMode::Fill;
    s.report["units_per_metre"] = s.scale;
    s.report["render_size"] = {window.width, window.height};
    s.report["camera"] = "existing third-person rig + late recentered head pose";
    s.report["excluded"] = {"HUD", "water refraction", "screen-space composites", "other game modes"};
    try {
        if (s.synthetic.empty()) {
            s.runtime.Initialize(s.report);
            renderer.vulkan_provider = &s.runtime;
        } else {
            if (!std::filesystem::create_directory(s.synthetic)) {
                throw Failure("synthetic_directory", 0, "Use a NEW capture directory with an existing parent");
            }
            s.capture_created = true;
            s.report["synthetic"] = true;
        }
    } catch (const std::exception &failure) {
        s.FailureReport(failure);
        throw;
    }
}

void GameBridge::Start() {
    auto &s = *impl_;
    try {
        if (s.synthetic.empty()) {
            s.runtime.CreateSession(gfx::ActiveVulkanContext());
            s.runtime.Loop().AsyncWait(true);
            s.controls = SDL_CreateRenderer(WindowHandle(), "software");
            if (!s.controls) {
                throw std::runtime_error(SDL_GetError());
            }
            SDL_SetWindowTitle(WindowHandle(), "DCVR - Norune test - F9 recenter / Esc exit");
            InputSetHostPaused(true);
            s.input_blocked = true;
        } else {
            s.head.Center(s.SyntheticPose());
        }
        s.start = Impl::Clock::now();
        s.started = true;
        Impl::active = &s;
        GameSetVrEnabled(true);
        GameSetPresentHook(Impl::Hook);
        WindowAddEventHook(Impl::Event);
    } catch (const std::exception &failure) {
        s.FailureReport(failure);
        throw;
    }
}

void GameBridge::Pump() {
    auto &s = *impl_;
    if (!s.started || s.failed) {
        return;
    }
    try {
        if (s.synthetic.empty()) {
            auto &loop = s.runtime.Loop();
            loop.Poll();
            if (loop.Exiting()) {
                GameRequestStop();
            }
            if (!GameVrScene() && !gfx::InFrame() && !gfx::Recording()) {
                loop.Frame({}, [] {}, false); // Loading/menus do not reuse stale world images.
                s.scene_ready = false;
                s.last_list = nullptr;
            }
            bool blocked = !loop.Focused() || SDL_GetKeyboardFocus() != WindowHandle();
            InputSetHostPaused(blocked);
            s.input_blocked = blocked;
            s.Ui();
        }
        if (Impl::Clock::now() - s.start >= std::chrono::seconds(s.seconds)) {
            GameRequestStop();
        }
    } catch (const std::exception &failure) {
        s.FailureReport(failure);
    }
}

void GameBridge::Stop() {
    auto &s = *impl_;
    if (s.stopped) {
        return;
    }
    s.stopped = true;
    GameSetPresentHook(nullptr);
    GameSetVrEnabled(false);
    // Capture requires the scene gate, so temporarily keep it enabled for final synthetic views.
    if (s.started && !s.synthetic.empty() && !s.failed) {
        GameSetVrEnabled(true);
        try {
            s.Capture();
        } catch (const std::exception &failure) {
            s.FailureReport(failure);
        }
        GameSetVrEnabled(false);
    }
    if (s.started) {
        WindowRemoveEventHook(Impl::Event);
    }
    Impl::active = nullptr;
    if (s.input_blocked) {
        InputSetHostPaused(false);
    }
    if (s.started && s.synthetic.empty()) {
        // Ask the runtime to close normally. A pending wait has its own worker; keep pumping
        // empty frames until STOPPING/EXITING or the bounded grace period expires.
        if (!s.failed) {
            try {
                auto &loop = s.runtime.Loop();
                loop.RequestExit();
                auto until = Impl::Clock::now() + std::chrono::seconds(2);
                while (!loop.Exiting() && Impl::Clock::now() < until) {
                    loop.Poll();
                    loop.Frame({}, [] {}, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            } catch (const std::exception &failure) {
                s.FailureReport(failure);
            }
        }
        s.report["frames_waited"] = s.runtime.Loop().Frames();
        s.report["frames_submitted"] = s.runtime.Loop().Submitted();
        if (!s.runtime.Loop().Submitted() && !s.failed) {
            s.FailureReport(Failure("game_submission", 0, "No tracked Norune frames submitted; headset test incomplete"));
        }
    }
    s.runtime.CloseSession(); // Includes joining the wait worker, before Vulkan teardown.
    if (s.mirror) {
        SDL_DestroyTexture(s.mirror);
        s.mirror = nullptr;
    }
    if (s.controls) {
        SDL_DestroyRenderer(s.controls);
        s.controls = nullptr;
    }
    s.report["eye_renders"] = s.eye_renders;
    s.report["synthetic_display_frames"] = s.synthetic_frames;
    s.report["canonical_ticks"] = GamePresentStatistics().ticks;
    s.report["game_frames"] = GameFrameCount();
    s.report["camera_frames_sampled"] = s.sampled_camera_frames;
    s.report["camera_changes"] = s.camera_changes;
    s.report["avatar_start_game_units"] = s.first_avatar;
    s.report["avatar_end_game_units"] = s.last_avatar;
    s.report["avatar_distance_walked_game_units"] = s.walked_units;
    s.report["operator_mirror_frames"] = s.mirror_frames;
    if (!s.failed) {
        s.report["status"] = s.synthetic.empty() ? "norune_frames_submitted" : "synthetic_norune_captured";
    }
    auto          path = s.capture_created ? s.synthetic / "capture.json" : PathsSaveRoot() / "dcvr-report.json";
    std::ofstream output(path);
    output << s.report.dump(2) << '\n';
    output.close();
    if (output.fail()) {
        throw std::runtime_error("Cannot write DCVR run receipt");
    }
    std::fprintf(stderr, "DCVR receipt: %s\n", PathsDisplay(path).c_str());
}

bool GameBridge::Succeeded() const { return !impl_->failed; }
} // namespace dcvr
