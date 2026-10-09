#pragma once

#include <openxr/openxr.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "gfx/vulkan.hpp"

namespace dcvr {

struct Failure : std::runtime_error {
    std::string stage;
    int         code;

    Failure(const char *s, int c, const std::string &why) : std::runtime_error(why), stage(s), code(c) {}
};

inline void Check(XrResult result, const char *call) {
    if (XR_FAILED(result)) {
        if (result == XR_ERROR_RUNTIME_UNAVAILABLE) {
            throw Failure(call, int(result), "XR_ERROR_RUNTIME_UNAVAILABLE: install/start Meta Horizon Link and configure its OpenXR runtime");
        }
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            throw Failure(call, int(result), "XR_ERROR_FORM_FACTOR_UNAVAILABLE: pair Quest 2 and launch Air Link inside the headset");
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

inline std::string Version(XrVersion value) {
    return std::to_string(XR_VERSION_MAJOR(value)) + "." + std::to_string(XR_VERSION_MINOR(value)) + "." +
           std::to_string(XR_VERSION_PATCH(value));
}

// OpenXR Vulkan bounds use major/minor, ignoring patch. Be conservative about the tested maximum.
inline uint32_t VulkanVersion(XrVersion minimum, XrVersion maximum, uint32_t loader) {
    auto      version = [](XrVersion v) { return XR_MAKE_VERSION(XR_VERSION_MAJOR(v), XR_VERSION_MINOR(v), 0); };
    XrVersion requested = std::max(version(minimum), XR_MAKE_VERSION(1, 3, 0));
    XrVersion available = XR_MAKE_VERSION(VK_API_VERSION_MAJOR(loader), VK_API_VERSION_MINOR(loader), 0);
    if (requested > version(maximum) || requested > available) {
        throw Failure("vulkan_version", 0, "No Vulkan 1.3+ version shared by Chronicle, the loader and runtime");
    }
    return VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(requested), XR_VERSION_MINOR(requested), 0);
}

} // namespace dcvr
