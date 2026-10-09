// Capability probe only: no session, game data, saves, window, or runtime-setting changes.
#include <cstdio>
#include <iostream>

#include "runtime.hpp"

int main(int argc, char **argv) {
    if (argc != 1) {
        std::fprintf(stderr, "usage: %s\nWrites an OpenXR/Vulkan JSON receipt; does not start VR gameplay.\n", argv[0]);
        return 2;
    }
    nlohmann::json report = {
        {"schema",            1            },
        {"status",            "unavailable"},
        {"headset_validated", false        },
        {"session_created",   false        },
        {"runtime_changed",   false        }
    };
    int           status = 0;
    dcvr::Runtime runtime;
    VkInstance    instance = VK_NULL_HANDLE;
    try {
        runtime.Initialize(report);
        uint32_t loader = VK_API_VERSION_1_0;
        VkResult result = vkEnumerateInstanceVersion(&loader);
        if (result != VK_SUCCESS) {
            throw dcvr::Failure("vulkan_loader", int(result), "Cannot query Vulkan loader");
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Chronicle DCVR probe";
        app.apiVersion = runtime.ApiVersion(loader);
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &app;
        result = runtime.CreateInstance(create, instance);
        if (result != VK_SUCCESS) {
            throw dcvr::Failure("create_vulkan_instance", int(result), "Vulkan instance creation failed");
        }
        runtime.PhysicalDevice(instance);
        report["status"] = "graphics_prerequisites_ready";
    } catch (const dcvr::Failure &failure) {
        report["failure"] = {
            {"stage",   failure.stage },
            {"code",    failure.code  },
            {"message", failure.what()}
        };
        std::fprintf(stderr, "DCVR: %s: %s (%d). See docs/DCVR.md for setup.\n",
                     failure.stage.c_str(), failure.what(), failure.code);
        status = 1;
    } catch (const std::exception &failure) {
        report["failure"] = {
            {"stage",   "probe"       },
            {"message", failure.what()}
        };
        status = 1;
    }
    if (instance) {
        vkDestroyInstance(instance, nullptr);
    }
    std::cout << report.dump(2) << '\n';
    return status;
}
