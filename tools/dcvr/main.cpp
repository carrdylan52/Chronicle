// A capability probe, not a headset rendering loop. No game data, saves, window, session,
// runtime registration writes or headset submission. stdout is one JSON receipt; loader logs
// and explanations go to stderr. Exit 0 means graphics prerequisites only, never playable VR.
#include <nlohmann/json.hpp>
#include <openxr/openxr.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gfx/requirements.hpp"

// openxr_platform.h requires Vulkan's declarations (including portability types above).
#if defined(XR_USE_GRAPHICS_API_VULKAN)
#include <openxr/openxr_platform.h>
#endif

using nlohmann::json;

namespace {
struct Context {
    XrInstance xr = XR_NULL_HANDLE;
    VkInstance vk = VK_NULL_HANDLE;

    ~Context() {
        if (vk) {
            vkDestroyInstance(vk, nullptr);
        }
        if (xr) {
            xrDestroyInstance(xr);
        }
    }
};

struct Failure : std::runtime_error {
    std::string stage;
    int         code;

    Failure(const char *s, int c, const std::string &why) : std::runtime_error(why), stage(s), code(c) {}
};

void Check(XrResult result, const char *call) {
    if (XR_FAILED(result)) {
        if (result == XR_ERROR_RUNTIME_UNAVAILABLE) {
            throw Failure(call, int(result), "XR_ERROR_RUNTIME_UNAVAILABLE: install/start Meta Horizon Link and configure its OpenXR runtime");
        }
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            throw Failure(call, int(result), "XR_ERROR_FORM_FACTOR_UNAVAILABLE: connect Quest 2 and launch Link inside the headset");
        }
        throw Failure(call, int(result), "OpenXR call failed");
    }
}

template <class T>
T Function(XrInstance instance, const char *name) {
    PFN_xrVoidFunction address = nullptr;
    Check(xrGetInstanceProcAddr(instance, name, &address), name);
    if (!address) {
        throw Failure(name, int(XR_ERROR_FUNCTION_UNSUPPORTED), "Null OpenXR entry point");
    }
    return reinterpret_cast<T>(address);
}

std::string Version(XrVersion value) {
    return std::to_string(XR_VERSION_MAJOR(value)) + "." + std::to_string(XR_VERSION_MINOR(value)) + "." +
           std::to_string(XR_VERSION_PATCH(value));
}

void Probe(json &report) {
    Context  context;
    uint32_t count = 0;
    Check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr), "enumerate_extensions");
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    Check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()), "enumerate_extensions");
    extensions.resize(count);
    bool vulkan2 = false;
    report["extensions"] = json::array();
    for (const auto &extension : extensions) {
        report["extensions"].push_back(extension.extensionName);
        vulkan2 |= std::strcmp(extension.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0;
    }
    if (!vulkan2) {
        throw Failure("vulkan_extension", int(XR_ERROR_EXTENSION_NOT_PRESENT),
                      "The active runtime does not expose XR_KHR_vulkan_enable2");
    }
    const char          *enabled[] = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(info.applicationInfo.applicationName, "Chronicle DCVR probe");
    std::strcpy(info.applicationInfo.engineName, "Chronicle");
    // enable2 needs only OpenXR 1.0, so do not unnecessarily exclude a 1.0 runtime.
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = enabled;
    Check(xrCreateInstance(&info, &context.xr), "create_instance");
    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    Check(xrGetInstanceProperties(context.xr, &properties), "runtime_properties");
    report["runtime"] = {
        {"name",    properties.runtimeName            },
        {"version", Version(properties.runtimeVersion)}
    };

    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    Check(xrGetSystem(context.xr, &system_info, &system), "get_headset_system");
    XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
    Check(xrGetSystemProperties(context.xr, system, &system_properties), "headset_properties");
    report["headset"] = {
        {"name",                 system_properties.systemName                                       },
        {"orientation_tracking", system_properties.trackingProperties.orientationTracking == XR_TRUE},
        {"position_tracking",    system_properties.trackingProperties.positionTracking == XR_TRUE   }
    };
    if (!system_properties.trackingProperties.orientationTracking || !system_properties.trackingProperties.positionTracking) {
        throw Failure("tracking_capability", 0, "DCVR requires positional and rotational tracking capability");
    }
    count = 0;
    Check(xrEnumerateViewConfigurationViews(context.xr, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                            0, &count, nullptr),
          "stereo_views");
    if (count != 2) {
        throw Failure("stereo_views", 0, "Expected exactly two PRIMARY_STEREO views");
    }
    std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    Check(xrEnumerateViewConfigurationViews(context.xr, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                            count, &count, views.data()),
          "stereo_views");
    report["views"] = json::array();
    for (const auto &view : views) {
        report["views"].push_back({
            {"width",   view.recommendedImageRectWidth      },
            {"height",  view.recommendedImageRectHeight     },
            {"samples", view.recommendedSwapchainSampleCount}
        });
    }

    auto                             requirements = Function<PFN_xrGetVulkanGraphicsRequirements2KHR>(context.xr, "xrGetVulkanGraphicsRequirements2KHR");
    XrGraphicsRequirementsVulkan2KHR required{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    Check(requirements(context.xr, system, &required), "vulkan_requirements");
    report["vulkan_range"] = {
        {"min", Version(required.minApiVersionSupported)},
        {"max", Version(required.maxApiVersionSupported)}
    };
    uint32_t loader_version = VK_API_VERSION_1_0;
    VkResult vk_result = vkEnumerateInstanceVersion(&loader_version);
    if (vk_result != VK_SUCCESS) {
        throw Failure("vulkan_loader", int(vk_result), "Cannot query Vulkan loader");
    }
    // Negotiate using major/minor: OpenXR's maximum includes every patch of that Vulkan version.
    const XrVersion runtime_min = XR_MAKE_VERSION(XR_VERSION_MAJOR(required.minApiVersionSupported),
                                                  XR_VERSION_MINOR(required.minApiVersionSupported), 0);
    const XrVersion runtime_max = XR_MAKE_VERSION(XR_VERSION_MAJOR(required.maxApiVersionSupported),
                                                  XR_VERSION_MINOR(required.maxApiVersionSupported), 0);
    const XrVersion requested = std::max(XR_MAKE_VERSION(1, 3, 0), runtime_min);
    const XrVersion loader = XR_MAKE_VERSION(VK_API_VERSION_MAJOR(loader_version), VK_API_VERSION_MINOR(loader_version), 0);
    if (requested > runtime_max || requested > loader) {
        throw Failure("vulkan_version", 0, "No common Vulkan version >= 1.3 for Chronicle and this runtime");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Chronicle DCVR probe";
    app.apiVersion = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(requested), XR_VERSION_MINOR(requested), 0);
    VkInstanceCreateInfo vk_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vk_info.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR create_info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    create_info.systemId = system;
    create_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    create_info.vulkanCreateInfo = &vk_info;
    auto create = Function<PFN_xrCreateVulkanInstanceKHR>(context.xr, "xrCreateVulkanInstanceKHR");
    Check(create(context.xr, &create_info, &context.vk, &vk_result), "create_vulkan_instance");
    if (vk_result != VK_SUCCESS) {
        throw Failure("create_vulkan_instance", int(vk_result), "Vulkan instance creation failed");
    }
    XrVulkanGraphicsDeviceGetInfoKHR gpu_info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gpu_info.systemId = system;
    gpu_info.vulkanInstance = context.vk;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    auto             get_gpu = Function<PFN_xrGetVulkanGraphicsDevice2KHR>(context.xr, "xrGetVulkanGraphicsDevice2KHR");
    Check(get_gpu(context.xr, &gpu_info, &gpu), "runtime_gpu");
    if (!gpu) {
        throw Failure("runtime_gpu", 0, "Runtime returned no Vulkan GPU");
    }
    VkPhysicalDeviceProperties gpu_properties;
    vkGetPhysicalDeviceProperties(gpu, &gpu_properties);
    report["gpu"] = {
        {"name",      gpu_properties.deviceName},
        {"vendor_id", gpu_properties.vendorID  },
        {"device_id", gpu_properties.deviceID  }
    };
    report["vulkan_requested"] = Version(requested);
    const auto caps = gfx::detail::QueryDeviceCaps(gpu, VK_NULL_HANDLE);
    auto       missing = gfx::detail::MissingRequirements(caps, true);
    if (!caps.features13.shaderDemoteToHelperInvocation) {
        missing.emplace_back("shaderDemoteToHelperInvocation");
    }
    if (caps.api_version < app.apiVersion) {
        missing.emplace_back("negotiated Vulkan API version");
    }
    report["missing_renderer_requirements"] = missing;
    if (!missing.empty()) {
        throw Failure("renderer_requirements", 0, "Runtime-selected GPU cannot run Chronicle");
    }
    report["status"] = "graphics_prerequisites_ready";
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 1) {
        std::fprintf(stderr, "usage: %s\nWrites an OpenXR/Vulkan JSON receipt to stdout; does not start VR gameplay.\n", argv[0]);
        return 2;
    }
    json report = {
        {"schema",            1            },
        {"status",            "unavailable"},
        {"headset_validated", false        },
        {"session_created",   false        },
        {"runtime_changed",   false        }
    };
    int status = 0;
    try {
        Probe(report);
    } catch (const Failure &failure) {
        report["failure"] = {
            {"stage",   failure.stage },
            {"code",    failure.code  },
            {"message", failure.what()}
        };
        std::fprintf(stderr, "DCVR: %s: %s (%d). See docs/DCVR.md for PC/headset setup.\n",
                     failure.stage.c_str(), failure.what(), failure.code);
        status = 1;
    } catch (const std::exception &failure) {
        report["failure"] = {
            {"stage",   "probe"       },
            {"message", failure.what()}
        };
        status = 1;
    }
    std::cout << report.dump(2) << '\n';
    return status;
}
