#include "VulkanSwapchain.h"
#include "VulkanDevice.h"
#include "VulkanHelpers.h"

#include <algorithm>
#include <limits>

namespace VulkanBaseplate {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Public API
    // ─────────────────────────────────────────────────────────────────────────────

    bool VulkanSwapchain::Initialize(VulkanDevice& device, VkSurfaceKHR surface,
        uint32_t width, uint32_t height)
    {
        m_Device = &device;
        m_Surface = surface;

        LogVk("[Swapchain] Initializing (" +
            std::to_string(width) + "x" + std::to_string(height) + ")...");

        if (!CreateSwapchain(width, height))
        {
            LogVk("[Swapchain] ERROR — creation failed");
            return false;
        }

        LogVk("[Swapchain] Initialized OK — " +
            std::to_string(m_Images.size()) + " images, extent " +
            std::to_string(m_Extent.width) + "x" + std::to_string(m_Extent.height));

        return true;
    }

    void VulkanSwapchain::Shutdown()
    {
        DestroySwapchainResources();
        m_Device = nullptr;
        m_Surface = VK_NULL_HANDLE;
    }

    bool VulkanSwapchain::Recreate(uint32_t width, uint32_t height)
    {
        // Ignore degenerate sizes (minimized window)
        if (width == 0 || height == 0)
            return true;

        LogVk("[Swapchain] Recreating (" +
            std::to_string(width) + "x" + std::to_string(height) + ")...");

        // Wait for all in-flight work to complete before touching the swapchain
        vkDeviceWaitIdle(m_Device->GetDevice());

        DestroySwapchainResources();

        if (!CreateSwapchain(width, height))
        {
            LogVk("[Swapchain] ERROR — recreation failed");
            return false;
        }

        LogVk("[Swapchain] Recreated OK — extent " +
            std::to_string(m_Extent.width) + "x" + std::to_string(m_Extent.height));

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Swapchain creation
    // ─────────────────────────────────────────────────────────────────────────────

    bool VulkanSwapchain::CreateSwapchain(uint32_t width, uint32_t height)
    {
        VulkanDevice::SwapchainSupportDetails support =
            m_Device->QuerySwapchainSupport(m_Surface);

        VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.Formats);
        VkPresentModeKHR   presentMode = ChoosePresentMode(support.PresentModes);
        VkExtent2D         extent = ChooseExtent(support.Capabilities, width, height);

        // Request one extra image over the minimum to avoid stalling on driver
        uint32_t imageCount = support.Capabilities.minImageCount + 1;
        if (support.Capabilities.maxImageCount > 0)
            imageCount = std::min(imageCount, support.Capabilities.maxImageCount);

        const char* presentModeStr = "FIFO";
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)    presentModeStr = "Mailbox";
        else if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)  presentModeStr = "Immediate";

        LogVk(std::string("[Swapchain] Format: ") + std::to_string(surfaceFormat.format) +
            " | PresentMode: " + presentModeStr +
            " | Images: " + std::to_string(imageCount));

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = m_Surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = surfaceFormat.format;
        swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapchainInfo.imageExtent = extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.preTransform = support.Capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presentMode;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE; // always fresh after Recreate()

        const QueueFamilyIndices& queueIndices = m_Device->GetQueueFamilyIndices();

        if (!queueIndices.IsCombined())
        {
            // Two distinct families — images must be shared between them
            uint32_t queueFamilyArray[] = {
                queueIndices.GraphicsFamily.value(),
                queueIndices.PresentFamily.value()
            };
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainInfo.queueFamilyIndexCount = 2;
            swapchainInfo.pQueueFamilyIndices = queueFamilyArray;
        }
        else
        {
            // Same family — exclusive is faster
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainInfo.queueFamilyIndexCount = 0;
            swapchainInfo.pQueueFamilyIndices = nullptr;
        }

        VK_CHECK(vkCreateSwapchainKHR(m_Device->GetDevice(), &swapchainInfo, nullptr, &m_Swapchain));

        // ── Retrieve images ──────────────────────────────────────────────────────
        uint32_t actualImageCount = 0;
        vkGetSwapchainImagesKHR(m_Device->GetDevice(), m_Swapchain, &actualImageCount, nullptr);
        m_Images.resize(actualImageCount);
        vkGetSwapchainImagesKHR(m_Device->GetDevice(), m_Swapchain, &actualImageCount, m_Images.data());

        m_ImageFormat = surfaceFormat.format;
        m_Extent = extent;

        // ── Create image views ───────────────────────────────────────────────────
        m_ImageViews.resize(m_Images.size());

        for (size_t i = 0; i < m_Images.size(); ++i)
        {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_Images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_ImageFormat;

            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VK_CHECK(vkCreateImageView(m_Device->GetDevice(), &viewInfo, nullptr, &m_ImageViews[i]));
        }

        return true;
    }

    void VulkanSwapchain::DestroySwapchainResources()
    {
        if (m_Device == nullptr)
            return;

        VkDevice device = m_Device->GetDevice();

        for (VkImageView view : m_ImageViews)
            vkDestroyImageView(device, view, nullptr);
        m_ImageViews.clear();

        // Images are owned by the swapchain — do NOT call vkDestroyImage on them
        m_Images.clear();

        if (m_Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Selection helpers
    // ─────────────────────────────────────────────────────────────────────────────

    VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& available) const
    {
        // ── Format preference order ───────────────────────────────────────────
        //
        // We prefer R8G8B8A8_UNORM over B8G8R8A8_* to avoid RGB/BGR channel
        // confusion.  UNORM is generally preferred for the swapchain format.
        // original D3D7 fixed-function pipeline which performed no gamma
        // conversion — all vertex colours and textures are stored in display
        // (gamma-encoded) space and are used as-is.
        //
        // Priority:
        //   1. VK_FORMAT_R8G8B8A8_UNORM  — preferred (RGBA, linear write)
        //   2. VK_FORMAT_R8G8B8A8_SRGB   — acceptable (RGBA, sRGB encode)
        //   3. VK_FORMAT_B8G8R8A8_UNORM  — fallback  (BGRA, linear write)
        //   4. VK_FORMAT_B8G8R8A8_SRGB   — last resort

        const VkFormat k_PreferredFormats[] = {
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_B8G8R8A8_SRGB,
        };

        for (VkFormat preferred : k_PreferredFormats)
        {
            for (const VkSurfaceFormatKHR& avail : available)
            {
                if (avail.format    == preferred &&
                    avail.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    LogVk("[Swapchain] Selected surface format: " +
                          std::to_string(avail.format));
                    return avail;
                }
            }
        }

        // Last resort: use whatever the driver gives us first.
        LogVk("[Swapchain] No preferred surface format found — using fallback: " +
              std::to_string(available[0].format));
        return available[0];
    }

    VkPresentModeKHR VulkanSwapchain::ChoosePresentMode(
        const std::vector<VkPresentModeKHR>& available) const
    {
        // Prefer mailbox (triple-buffer, no tearing, low latency)
        for (VkPresentModeKHR mode : available)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                LogVk("[Swapchain] Present mode: Mailbox");
                return mode;
            }
        }

        // FIFO is guaranteed to be available (vsync)
        LogVk("[Swapchain] Present mode: FIFO (vsync)");
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanSwapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
        uint32_t width, uint32_t height) const
    {
        // If the driver has already locked the extent, use it as-is
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return capabilities.currentExtent;

        // Otherwise clamp our requested size to what the surface supports
        VkExtent2D actual = { width, height };
        actual.width = std::clamp(actual.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actual.height = std::clamp(actual.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actual;
    }

} // namespace VulkanBaseplate