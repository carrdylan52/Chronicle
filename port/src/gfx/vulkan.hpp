#pragma once

#include "requirements.hpp"

namespace gfx {

// Optional graphics creation supplied by an external presentation runtime. The renderer owns
// the returned instance/device. The provider must outlive it; destroy external sessions first.
// Desktop builds have no OpenXR dependency and use the normal Vulkan creation path.
class VulkanProvider {
public:
    virtual ~VulkanProvider() = default;
    virtual uint32_t         ApiVersion(uint32_t loader_version) const = 0;
    virtual VkResult         CreateInstance(const VkInstanceCreateInfo &info, VkInstance &instance) = 0;
    virtual VkPhysicalDevice PhysicalDevice(VkInstance instance) = 0;
    virtual VkResult         CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo &info,
                                          VkDevice &device) = 0;
};

struct VulkanContext {
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family;
};

// Borrowed handles, valid only between RendererInit and RendererShutdown.
VulkanContext ActiveVulkanContext();

// Outside recording/rendering, after a successful display RenderList with present=true.
// Offscreen renderers still perform no desktop presentation. Blits the display target
// to an externally owned, acquired single-layer colour image on the renderer's device/queue.
// Caller guarantees transfer-destination usage, a blit-capable format and exclusive ownership.
// The image starts and finishes COLOR_ATTACHMENT_OPTIMAL, as OpenXR requires. This prototype
// waits for GPU completion before returning, so a later eye cannot overwrite the source and
// the caller can release the destination safely. No desktop presentation or simulation runs.
bool CopyDisplayToVulkanImage(VkImage image, VkFormat format, uint32_t width, uint32_t height);

} // namespace gfx
