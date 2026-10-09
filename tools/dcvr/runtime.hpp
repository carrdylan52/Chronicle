#pragma once

#include <nlohmann/json.hpp>

#include <memory>

#include "frame_loop.hpp"

namespace dcvr {

// OpenXR owns session/spaces/swapchains; Chronicle owns the negotiated Vulkan instance/device.
// Initialize before RendererInit, CloseSession before RendererShutdown, destroy Runtime last.
class Runtime final : public gfx::VulkanProvider {
public:
    Runtime() = default;
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    void     Initialize(nlohmann::json &report);
    void     CreateSession(const gfx::VulkanContext &graphics);
    void     CloseSession() noexcept;

    FrameLoop &Loop() { return *loop_; }

    const std::array<XrViewConfigurationView, 2> &ViewSizes() const { return sizes_; }

    uint32_t         ApiVersion(uint32_t loader_version) const override;
    VkResult         CreateInstance(const VkInstanceCreateInfo &info, VkInstance &instance) override;
    VkPhysicalDevice PhysicalDevice(VkInstance instance) override;
    VkResult         CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo &info, VkDevice &device) override;

private:
    XrInstance                             instance_ = XR_NULL_HANDLE;
    XrSystemId                             system_ = XR_NULL_SYSTEM_ID;
    XrSession                              session_ = XR_NULL_HANDLE;
    XrSpace                                local_ = XR_NULL_HANDLE, head_ = XR_NULL_HANDLE;
    XrGraphicsRequirementsVulkan2KHR       requirements_{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    std::array<XrViewConfigurationView, 2> sizes_{};
    std::array<EyeSwapchain, 2>            eyes_{};
    std::unique_ptr<FrameLoop>             loop_;
    VkInstance                             vk_instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice                       physical_ = VK_NULL_HANDLE;
    VkDevice                               device_ = VK_NULL_HANDLE;
    nlohmann::json                        *report_ = nullptr;
};

} // namespace dcvr
