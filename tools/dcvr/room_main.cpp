#include <SDL3/SDL.h>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <numbers>

#include "platform/paths.hpp"
#include "room.hpp"
#include "runtime.hpp"

namespace {
struct Options {
    int                   frames = 600;
    int                   seconds = 60;
    std::filesystem::path synthetic;
};

Options Parse(int argc, const char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string_view key = argv[i];
        if (i + 1 == argc) {
            throw std::invalid_argument("Missing argument value");
        }
        std::string_view value = argv[++i];
        if (key == "--synthetic") {
            options.synthetic = PathsFromUtf8(value);
        } else if (key == "--frames" || key == "--seconds") {
            int number = 0;
            auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
            if (error != std::errc() || end != value.data() + value.size() || number <= 0 || number > 100000) {
                throw std::invalid_argument("Expected a positive bounded number");
            }
            (key == "--frames" ? options.frames : options.seconds) = number;
        } else {
            throw std::invalid_argument("Unknown argument");
        }
    }
    return options;
}

struct Host {
    dcvr::Runtime *runtime = nullptr;
    SDL_Window    *target = nullptr, *controls = nullptr;
    SDL_Renderer  *ui = nullptr;

    ~Host() {
        if (runtime) {
            runtime->CloseSession();
        }
        gfx::RendererShutdown();
        if (ui) {
            SDL_DestroyRenderer(ui);
        }
        if (controls) {
            SDL_DestroyWindow(controls);
        }
        if (target) {
            SDL_DestroyWindow(target);
        }
        SDL_Quit();
    }

    void Init(int width, int height, gfx::VulkanProvider *provider) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(SDL_GetError());
        }
        target = SDL_CreateWindow("DCVR eye target", width, height, SDL_WINDOW_HIDDEN);
        if (!target) {
            throw std::runtime_error(SDL_GetError());
        }
        gfx::RendererConfig config;
        config.offscreen = true;
        config.render_scale = 1;
        config.pipeline_cache.clear(); // No game/save directory or cache file writes.
        config.vulkan_provider = provider;
        gfx::RendererInit(target, config);
    }

    void Controls() {
        controls = SDL_CreateWindow("DCVR calibration - keyboard controls", 640, 320, 0);
        if (!controls) {
            throw std::runtime_error(SDL_GetError());
        }
        ui = SDL_CreateRenderer(controls, "software");
        if (!ui) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetRenderDrawColor(ui, 18, 22, 32, 255);
        SDL_RenderClear(ui);
        SDL_SetRenderScale(ui, 2, 2);
        SDL_SetRenderDrawColor(ui, 230, 235, 245, 255);
        SDL_RenderDebugText(ui, 12, 15, "DCVR calibration room");
        SDL_RenderDebugText(ui, 12, 38, "Put on Quest and launch Air Link.");
        SDL_RenderDebugText(ui, 12, 60, "WASD: walk  Q/E: turn 45 degrees");
        SDL_RenderDebugText(ui, 12, 82, "R: recenter  Esc: exit");
        SDL_RenderDebugText(ui, 12, 112, "Grid: 1 metre. Blue cube: 1 metre.");
        SDL_RenderDebugText(ui, 12, 134, "Keyboard requires this window focus.");
        SDL_RenderPresent(ui);
    }
};

void Capture(const gfx::DisplayList &room, const std::filesystem::path &directory) {
    dcvr::FrameViews frame{};
    frame.head.orientation.w = 1;
    dcvr::RoomCamera camera;
    camera.Center(frame);
    constexpr float angle = std::numbers::pi_v<float> / 4;
    for (int eye = 0; eye < 2; ++eye) {
        XrView view{XR_TYPE_VIEW};
        view.pose.orientation.w = 1;
        view.pose.position.x = eye == 0 ? -.032f : .032f;
        view.fov = {-angle, angle, angle, -angle};
        dcvr::RenderRoomEye(room, camera, view);
        std::vector<uint8_t> pixels;
        uint32_t             width = 0, height = 0;
        if (!gfx::ReadbackFrame(pixels, width, height) ||
            !gfx::WritePng(directory / (eye == 0 ? "left.png" : "right.png"), pixels.data(), width, height)) {
            throw dcvr::Failure("synthetic_capture", 0, "Cannot write room eye PNG");
        }
    }
}

void Run(const Options &options, nlohmann::json &report) {
    dcvr::Runtime runtime;
    Host          host;
    if (!options.synthetic.empty()) {
        if (!std::filesystem::create_directory(options.synthetic)) {
            throw dcvr::Failure("synthetic_directory", 0, "Use a new directory with an existing parent");
        }
        host.Init(960, 720, nullptr);
    } else {
        runtime.Initialize(report);
        host.runtime = &runtime;
        const auto &views = runtime.ViewSizes();
        host.Init(int(std::max(views[0].recommendedImageRectWidth, views[1].recommendedImageRectWidth)),
                  int(std::max(views[0].recommendedImageRectHeight, views[1].recommendedImageRectHeight)), &runtime);
        runtime.CreateSession(gfx::ActiveVulkanContext());
        host.Controls();
    }
    auto room = dcvr::RecordRoom();
    if (!gfx::RenderList(*room, 1, {.canonical = true})) {
        throw dcvr::Failure("canonical_room", 0, "Cannot render calibration room");
    }
    report["room_units_per_metre"] = 1;
    if (!options.synthetic.empty()) {
        Capture(*room, options.synthetic);
        report["status"] = "synthetic_room_captured";
        report["ipd_metres"] = .064;
        report["synthetic"] = true;
        std::ofstream file(options.synthetic / "capture.json");
        file << report.dump(2) << '\n';
        file.close();
        if (file.fail()) {
            throw dcvr::Failure("capture_manifest", 0, "Cannot write capture receipt");
        }
        return;
    }
    auto            &loop = runtime.Loop();
    dcvr::RoomCamera camera;
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now(), last = start;
    bool requesting_exit = false;
    auto exit_deadline = start;
    auto graphics = gfx::ActiveVulkanContext();
    auto request_exit = [&] {
        if (!requesting_exit) {
            loop.RequestExit();
            requesting_exit = true;
            exit_deadline = Clock::now() + std::chrono::seconds(2);
        }
    };
    while (!loop.Exiting()) {
        loop.Poll();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                request_exit();
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    request_exit();
                }
                if (loop.Focused()) {
                    if (event.key.scancode == SDL_SCANCODE_R) {
                        camera.Recenter();
                    }
                    if (event.key.scancode == SDL_SCANCODE_Q) {
                        camera.Turn(std::numbers::pi_v<float> / 4);
                    }
                    if (event.key.scancode == SDL_SCANCODE_E) {
                        camera.Turn(-std::numbers::pi_v<float> / 4);
                    }
                }
            }
        }
        auto now = Clock::now();
        if (std::chrono::duration<double>(now - start).count() >= options.seconds || loop.Frames() >= uint64_t(options.frames)) {
            request_exit();
        }
        if (requesting_exit && now >= exit_deadline) {
            break;
        }
        auto result = loop.Frame([&](uint32_t eye, const dcvr::FrameViews &frame, const dcvr::EyeSwapchain &swapchain, uint32_t image) {
            if (eye == 0) {
                auto current = Clock::now();
                float elapsed = std::chrono::duration<float>(current - last).count();
                last = current;
                camera.Center(frame);
                if (loop.Focused() && SDL_GetKeyboardFocus() == host.controls) {
                    const bool *keys = SDL_GetKeyboardState(nullptr);
                    camera.Move(float(keys[SDL_SCANCODE_W]) - float(keys[SDL_SCANCODE_S]),
                                float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]), elapsed);
                }
            }
            dcvr::RenderRoomEye(*room, camera, frame.eyes[eye]);
            if (!gfx::CopyDisplayToVulkanImage(swapchain.images[image].image, swapchain.format, swapchain.width, swapchain.height)) {
                throw dcvr::Failure("copy_eye_image", 0, "Cannot copy the rendered eye to its OpenXR image");
            } }, [&] { vkDeviceWaitIdle(graphics.device); });
        if (result == dcvr::FrameResult::Idle) {
            SDL_Delay(5);
        }
    }
    report["frames_waited"] = loop.Frames();
    report["frames_submitted"] = loop.Submitted();
    report["status"] = loop.Submitted() ? "calibration_frames_submitted" : "no_visible_calibration_frames";
    if (!loop.Submitted()) {
        throw dcvr::Failure("calibration_submission", 0, "No visible tracked frames submitted before exit/timeout");
    }
}
} // namespace

int RunMain(int argc, const char **argv) {
    Options options;
    try {
        options = Parse(argc, argv);
    } catch (const std::exception &failure) {
        std::fprintf(stderr, "%s\nusage: dcvr_room [--frames 600] [--seconds 60] [--synthetic NEW_DIR]\n", failure.what());
        return 2;
    }
    nlohmann::json report = {
        {"schema",            1                 },
        {"kind",              "calibration_room"},
        {"status",            "unavailable"     },
        {"headset_validated", false             },
        {"session_created",   false             },
        {"runtime_changed",   false             }
    };
    int status = 0;
    try {
        Run(options, report);
    } catch (const dcvr::Failure &failure) {
        report["failure"] = {
            {"stage",   failure.stage },
            {"code",    failure.code  },
            {"message", failure.what()}
        };
        std::fprintf(stderr, "DCVR: %s: %s (%d)\n", failure.stage.c_str(), failure.what(), failure.code);
        status = 1;
    } catch (const std::exception &failure) {
        report["failure"] = {
            {"stage",   "calibration_room"},
            {"message", failure.what()    }
        };
        status = 1;
    }
    std::cout << report.dump(2) << '\n';
    return status;
}

#ifdef _WIN32
int wmain(int argc, wchar_t **wide_argv) {
    std::vector<std::string>  args;
    std::vector<const char *> argv;
    for (int i = 0; i < argc; ++i) {
        args.push_back(PathsDisplay(std::filesystem::path(wide_argv[i])));
    }
    for (const auto &arg : args) {
        argv.push_back(arg.c_str());
    }
    return RunMain(argc, argv.data());
}
#else
int main(int argc, char **argv) {
    std::vector<const char *> args(argv, argv + argc);
    return RunMain(argc, args.data());
}
#endif
