#pragma once

#include <vulkan/vulkan.h>
#include "VulkanHelpers.h"   // VCK::Config, PresentMode
#include <vector>

namespace VCK {

    class VulkanDevice;
    class VulkanContext; // forward — avoid pulling in VulkanDevice.h transitively

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanSwapchain
    //
    //  Responsibilities:
    //    1. Choose surface format, present mode, and extent.
    //    2. Create VkSwapchainKHR with the correct image count and sharing mode.
    //    3. Retrieve swapchain images and create VkImageViews for each.
    //    4. Recreate cleanly on WM_SIZE (destroy old, build new).
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanSwapchain
    {
    public:
        VulkanSwapchain() = default;
        ~VulkanSwapchain() = default;

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        // device   — owns physical/logical device and queue family info
        // surface  — VkSurfaceKHR from VulkanContext
        // width/height — initial client area size
        // Preferred overload — pulls the surface from the context.
        // Config overloads let the caller pick presentMode / imageCount /
        // surfaceFormat / msaaSamples / depthFormat.
        bool Initialize(VulkanDevice& device, VulkanContext& context,
            uint32_t width, uint32_t height);
        bool Initialize(VulkanDevice& device, VulkanContext& context,
            uint32_t width, uint32_t height, const Config& cfg);

        bool Initialize(VulkanDevice& device, VkSurfaceKHR surface,
            uint32_t width, uint32_t height);
        bool Initialize(VulkanDevice& device, VkSurfaceKHR surface,
            uint32_t width, uint32_t height, const Config& cfg);

        void Shutdown();

        // Call from WM_SIZE handler (after vkDeviceWaitIdle)
        bool Recreate(uint32_t width, uint32_t height);

        // ── Accessors ────────────────────────────────────────────────────────────
        VkSwapchainKHR                    GetSwapchain()    const { return m_Swapchain; }
        VkFormat                          GetImageFormat()  const { return m_ImageFormat; }
        VkExtent2D                        GetExtent()       const { return m_Extent; }
        uint32_t                          GetImageCount()   const { return static_cast<uint32_t>(m_Images.size()); }
        const std::vector<VkImage>& GetImages()       const { return m_Images; }
        const std::vector<VkImageView>& GetImageViews()   const { return m_ImageViews; }

        // Read back cfg.swapchain knobs after init (VulkanPipeline uses these).
        VkSampleCountFlagBits            GetMSAASamples()         const { return m_CfgSwapchain.msaaSamples;  }
        VkFormat                         GetPreferredDepthFormat()const { return m_CfgSwapchain.depthFormat;  }

    private:
        bool CreateSwapchain(uint32_t width, uint32_t height);
        void DestroySwapchainResources();

        VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) const;
        VkPresentModeKHR   ChoosePresentMode(const std::vector<VkPresentModeKHR>& available) const;
        VkExtent2D         ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
            uint32_t width, uint32_t height) const;

        // ── Owned references (not owned by this class) ───────────────────────────
        VulkanDevice* m_Device = nullptr;
        VkSurfaceKHR   m_Surface = VK_NULL_HANDLE;

        // ── Swapchain state ──────────────────────────────────────────────────────
        VkSwapchainKHR           m_Swapchain = VK_NULL_HANDLE;
        VkFormat                 m_ImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D               m_Extent = { 0, 0 };

        std::vector<VkImage>     m_Images;
        std::vector<VkImageView> m_ImageViews;

        // Snapshot of cfg.swapchain (presentMode / imageCount / surfaceFormat /
        // msaaSamples / depthFormat).  Used by ChoosePresentMode / ChooseSurfaceFormat
        // / imageCount logic and surfaced via GetMSAASamples / GetPreferredDepthFormat.
        Config::SwapchainCfg     m_CfgSwapchain;
    };

} // namespace VCK
