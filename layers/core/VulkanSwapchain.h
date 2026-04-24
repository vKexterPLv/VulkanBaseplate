#pragma once

#include <vulkan/vulkan.h>
#include "VulkanHelpers.h"   // VCK::Config, PresentMode
#include "VulkanImage.h"     // MSAA resolve-source target
#include <vector>

namespace VCK {

    class VulkanDevice;
    class VulkanContext; // forward - avoid pulling in VulkanDevice.h transitively

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

        // device   - owns physical/logical device and queue family info
        // surface  - VkSurfaceKHR from VulkanContext
        // width/height - initial client area size
        // Preferred overload - pulls the surface from the context.
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

        // Call from WM_SIZE handler.  By default the swapchain internally
        // issues vkDeviceWaitIdle before touching its resources; pass
        // drainedExternally=true when the caller already waited for all
        // in-flight work to retire (e.g. via FrameScheduler::DrainInFlight)
        // so the redundant device-wide wait is skipped.  Rule 4: runtime
        // vkDeviceWaitIdle is only permitted via this internal fallback;
        // the scheduler-aware HandleLiveResize path sets the flag.
        bool Recreate(uint32_t width, uint32_t height, bool drainedExternally = false);

        // ── Accessors ────────────────────────────────────────────────────────────
        VkSwapchainKHR                    GetSwapchain()    const { return m_Swapchain; }
        VkFormat                          GetImageFormat()  const { return m_ImageFormat; }
        VkExtent2D                        GetExtent()       const { return m_Extent; }
        uint32_t                          GetImageCount()   const { return static_cast<uint32_t>(m_Images.size()); }
        const std::vector<VkImage>& GetImages()       const { return m_Images; }
        const std::vector<VkImageView>& GetImageViews()   const { return m_ImageViews; }

        // MSAA colour views, one per swapchain image.  Empty vector when
        // cfg.swapchain.msaaSamples == VK_SAMPLE_COUNT_1_BIT (no MSAA).  The
        // framebuffer binds MSAA view[i] as the colour attachment and the
        // swapchain image view as the single-sample resolve attachment.
        std::vector<VkImageView> GetMSAAColorViews() const;
        bool                     HasMSAA()           const { return m_CfgSwapchain.msaaSamples != VK_SAMPLE_COUNT_1_BIT; }

        // Read back cfg.swapchain knobs after init (VulkanPipeline uses these).
        VkSampleCountFlagBits            GetMSAASamples()         const { return m_CfgSwapchain.msaaSamples;  }
        VkFormat                         GetPreferredDepthFormat()const { return m_CfgSwapchain.depthFormat;  }

        // AA technique resolved at Initialize.  If cfg.aa.technique was
        // Auto it reflects the detector's pick (TAA / SMAA_1x / FXAA /
        // MSAA_A2C etc.).  Post-process techniques (IsPostProcessAA()) are
        // not implemented inside VCK - the renderer reads this value and
        // runs the matching post-process pass itself.
        AATechnique                      GetAATechnique()         const { return m_ResolvedAA;             }
        const Config::AACfg&             GetAACfg()               const { return m_CfgAA;                 }

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

        // Multisampled colour targets - one per swapchain image.  Only populated
        // when cfg.swapchain.msaaSamples > 1.  Each target is TRANSIENT (tile
        // memory on mobile GPUs, VRAM on desktop) and LAZILY_ALLOCATED if the
        // driver supports it.
        std::vector<VulkanImage> m_MsaaTargets;

        // Snapshot of cfg.swapchain (presentMode / imageCount / surfaceFormat /
        // msaaSamples / depthFormat).  Used by ChoosePresentMode / ChooseSurfaceFormat
        // / imageCount logic and surfaced via GetMSAASamples / GetPreferredDepthFormat.
        Config::SwapchainCfg     m_CfgSwapchain;

        // Snapshot of cfg.aa + the resolved technique.  m_ResolvedAA equals
        // m_CfgAA.technique unless the latter was AATechnique::Auto, in which
        // case DetectRecommendedAA fills it in.
        Config::AACfg            m_CfgAA;
        AATechnique              m_ResolvedAA = AATechnique::Off;
    };

} // namespace VCK
