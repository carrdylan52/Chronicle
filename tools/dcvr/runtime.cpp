#include "runtime.hpp"

#include <algorithm>
#include <cstring>

namespace dcvr {
using nlohmann::json;

Runtime::~Runtime() {
    CloseSession();
    if (instance_) {
        xrDestroyInstance(instance_);
    }
}

void Runtime::Initialize(json &report) {
    if (instance_) {
        throw Failure("initialize", 0, "OpenXR runtime already initialized");
    }
    report_ = &report;
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
        throw Failure("vulkan_extension", int(XR_ERROR_EXTENSION_NOT_PRESENT), "Active runtime lacks XR_KHR_vulkan_enable2");
    }
    const char          *enabled[] = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(info.applicationInfo.applicationName, "Chronicle DCVR");
    std::strcpy(info.applicationInfo.engineName, "Chronicle");
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = enabled;
    Check(xrCreateInstance(&info, &instance_), "create_instance");
    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    Check(xrGetInstanceProperties(instance_, &properties), "runtime_properties");
    report["runtime"] = {
        {"name",    properties.runtimeName            },
        {"version", Version(properties.runtimeVersion)}
    };
    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    Check(xrGetSystem(instance_, &system_info, &system_), "get_headset_system");
    XrSystemProperties properties_system{XR_TYPE_SYSTEM_PROPERTIES};
    Check(xrGetSystemProperties(instance_, system_, &properties_system), "headset_properties");
    report["headset"] = {
        {"name",                 properties_system.systemName                                       },
        {"orientation_tracking", properties_system.trackingProperties.orientationTracking == XR_TRUE},
        {"position_tracking",    properties_system.trackingProperties.positionTracking == XR_TRUE   }
    };
    if (!properties_system.trackingProperties.orientationTracking || !properties_system.trackingProperties.positionTracking) {
        throw Failure("tracking_capability", 0, "DCVR requires positional and rotational tracking capability");
    }
    Check(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                            0, &count, nullptr),
          "stereo_views");
    if (count != 2) {
        throw Failure("stereo_views", 0, "Expected exactly two PRIMARY_STEREO views");
    }
    for (auto &size : sizes_) {
        size.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    Check(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                            2, &count, sizes_.data()),
          "stereo_views");
    report["views"] = json::array();
    for (const auto &view : sizes_) {
        if (!view.recommendedImageRectWidth || !view.recommendedImageRectHeight) {
            throw Failure("stereo_views", 0, "Runtime returned an empty eye extent");
        }
        report["views"].push_back({
            {"width",   view.recommendedImageRectWidth      },
            {"height",  view.recommendedImageRectHeight     },
            {"samples", view.recommendedSwapchainSampleCount}
        });
    }
    auto requirements = Function<PFN_xrGetVulkanGraphicsRequirements2KHR>(instance_, "xrGetVulkanGraphicsRequirements2KHR");
    Check(requirements(instance_, system_, &requirements_), "vulkan_requirements");
    report["vulkan_range"] = {
        {"min", Version(requirements_.minApiVersionSupported)},
        {"max", Version(requirements_.maxApiVersionSupported)}
    };
}

uint32_t Runtime::ApiVersion(uint32_t loader_version) const {
    return VulkanVersion(requirements_.minApiVersionSupported, requirements_.maxApiVersionSupported, loader_version);
}

VkResult Runtime::CreateInstance(const VkInstanceCreateInfo &info, VkInstance &instance) {
    XrVulkanInstanceCreateInfoKHR create{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    create.systemId = system_;
    create.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    create.vulkanCreateInfo = &info;
    VkResult result = VK_SUCCESS;
    auto     function = Function<PFN_xrCreateVulkanInstanceKHR>(instance_, "xrCreateVulkanInstanceKHR");
    Check(function(instance_, &create, &instance, &result), "create_vulkan_instance");
    if (result == VK_SUCCESS) {
        vk_instance_ = instance;
        auto version = info.pApplicationInfo->apiVersion;
        (*report_)["vulkan_requested"] = Version(XR_MAKE_VERSION(VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), 0));
    }
    return result;
}

VkPhysicalDevice Runtime::PhysicalDevice(VkInstance instance) {
    XrVulkanGraphicsDeviceGetInfoKHR info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    info.systemId = system_;
    info.vulkanInstance = instance;
    auto get = Function<PFN_xrGetVulkanGraphicsDevice2KHR>(instance_, "xrGetVulkanGraphicsDevice2KHR");
    Check(get(instance_, &info, &physical_), "runtime_gpu");
    if (!physical_) {
        throw Failure("runtime_gpu", 0, "Runtime returned no Vulkan GPU");
    }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_, &properties);
    (*report_)["gpu"] = {
        {"name",      properties.deviceName},
        {"vendor_id", properties.vendorID  },
        {"device_id", properties.deviceID  }
    };
    const auto caps = gfx::detail::QueryDeviceCaps(physical_, VK_NULL_HANDLE);
    auto       missing = gfx::detail::MissingRequirements(caps, true);
    if (!caps.features13.shaderDemoteToHelperInvocation) {
        missing.emplace_back("shaderDemoteToHelperInvocation");
    }
    uint32_t loader = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loader) != VK_SUCCESS || caps.api_version < ApiVersion(loader)) {
        missing.emplace_back("negotiated Vulkan API version");
    }
    (*report_)["missing_renderer_requirements"] = missing;
    if (!missing.empty()) {
        throw Failure("renderer_requirements", 0, "Runtime-selected GPU cannot run Chronicle");
    }
    return physical_;
}

VkResult Runtime::CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo &info, VkDevice &device) {
    if (physical != physical_) {
        throw Failure("create_vulkan_device", 0, "Renderer GPU differs from OpenXR GPU");
    }
    XrVulkanDeviceCreateInfoKHR create{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    create.systemId = system_;
    create.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    create.vulkanPhysicalDevice = physical;
    create.vulkanCreateInfo = &info;
    VkResult result = VK_SUCCESS;
    auto     function = Function<PFN_xrCreateVulkanDeviceKHR>(instance_, "xrCreateVulkanDeviceKHR");
    Check(function(instance_, &create, &device, &result), "create_vulkan_device");
    if (result == VK_SUCCESS) {
        device_ = device;
        (*report_)["graphics_device_created"] = true;
    }
    return result;
}

void Runtime::CreateSession(const gfx::VulkanContext &graphics) {
    if (session_ || !device_ || graphics.instance != vk_instance_ || graphics.physical_device != physical_ || graphics.device != device_) {
        throw Failure("create_session", 0, "Session requires the renderer's runtime-negotiated Vulkan context");
    }
    uint32_t count = 0;
    Check(xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                           0, &count, nullptr),
          "blend_modes");
    std::vector<XrEnvironmentBlendMode> modes(count);
    Check(xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                           count, &count, modes.data()),
          "blend_modes");
    if (std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == modes.end()) {
        throw Failure("blend_modes", 0, "DCVR calibration requires opaque VR composition");
    }
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = graphics.instance;
    binding.physicalDevice = graphics.physical_device;
    binding.device = graphics.device;
    binding.queueFamilyIndex = graphics.queue_family;
    binding.queueIndex = 0;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = system_;
    Check(xrCreateSession(instance_, &info, &session_), "create_session");
    (*report_)["session_created"] = true;
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    Check(xrCreateReferenceSpace(session_, &space, &local_), "create_local_space");
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    Check(xrCreateReferenceSpace(session_, &space, &head_), "create_head_space");
    Check(xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "eye_formats");
    std::vector<int64_t> formats(count);
    Check(xrEnumerateSwapchainFormats(session_, count, &count, formats.data()), "eye_formats");
    VkFormat format = VK_FORMAT_UNDEFINED;
    for (VkFormat candidate : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
                               VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB}) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical_, candidate, &properties);
        if (std::find(formats.begin(), formats.end(), int64_t(candidate)) != formats.end() &&
            (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
            format = candidate;
            break;
        }
    }
    if (format == VK_FORMAT_UNDEFINED) {
        throw Failure("eye_formats", 0, "No supported RGBA/BGRA 8-bit blit destination eye format");
    }
    (*report_)["eye_format"] = int(format);
    (*report_)["eye_samples"] = 1;
    for (uint32_t i = 0; i < 2; ++i) {
        auto &eye = eyes_[i];
        eye.width = sizes_[i].recommendedImageRectWidth;
        eye.height = sizes_[i].recommendedImageRectHeight;
        eye.format = format;
        XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        create.format = format;
        create.sampleCount = 1;
        create.width = eye.width;
        create.height = eye.height;
        create.faceCount = create.arraySize = create.mipCount = 1;
        Check(xrCreateSwapchain(session_, &create, &eye.handle), "create_eye_swapchain");
        Check(xrEnumerateSwapchainImages(eye.handle, 0, &count, nullptr), "eye_images");
        if (!count) {
            throw Failure("eye_images", 0, "Runtime returned no eye images");
        }
        eye.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        Check(xrEnumerateSwapchainImages(eye.handle, count, &count,
                                         reinterpret_cast<XrSwapchainImageBaseHeader *>(eye.images.data())),
              "eye_images");
    }
    loop_ = std::make_unique<FrameLoop>(instance_, session_, local_, head_, eyes_);
}

void Runtime::CloseSession() noexcept {
    loop_.reset();
    if (session_ && device_) {
        vkDeviceWaitIdle(device_);
    }
    for (auto &eye : eyes_) {
        if (eye.handle) {
            xrDestroySwapchain(eye.handle);
        }
        eye = {};
    }
    if (head_) {
        xrDestroySpace(head_);
    }
    if (local_) {
        xrDestroySpace(local_);
    }
    if (session_) {
        xrDestroySession(session_);
    }
    head_ = local_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

} // namespace dcvr
