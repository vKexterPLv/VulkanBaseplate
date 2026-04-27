#include "VulkanSwapchain.h"
#include "VulkanDevice.h"
#include "VulkanContext.h"
#include "VulkanHelpers.h"

#include <algorithm>
#include <limits>

namespace VCK {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Public API
    // ─────────────────────────────────────────────────────────────────────────────

    // Preferred overload - pulls the surface out of the context.
    bool VulkanSwapchain::Initialize(VulkanDevice& device, VulkanContext& context,
        uint32_t width, uint32_t height)
    {
        return Initialize(device, context.GetSurface(), width, height);
    }

    bool VulkanSwapchain::Initialize(VulkanDevice& device, VulkanContext& context,
        uint32_t width, uint32_t height, const Config& cfg)
    {
        return Initialize(device, context.GetSurface(), width, height, cfg);
    }

    bool VulkanSwapchain::Initialize(VulkanDevice& device, VkSurfaceKHR surface,
        uint32_t width, uint32_t height, const Config& cfg)
    {
        m_CfgSwapchain = cfg.swapchain;
        m_CfgAA        = cfg.aa;
        return Initialize(device, surface, width, height);
    }

    bool VulkanSwapchain::Initialize(VulkanDevice& device, VkSurfaceKHR surface,
        uint32_t width, uint32_t height)
    {
        m_Device = &device;
        m_Surface = surface;

        // ── AA technique resolve ─────────────────────────────────────────────
        // Step 1: pick the AA family.  If the caller pinned cfg.aa.technique
        // we honour it as-is; otherwise DetectRecommendedAA walks the 5-step
        // decision tree (tier -> forward/deferred -> motion vectors).
        m_ResolvedAA = m_CfgAA.technique;
        if (m_ResolvedAA == AATechnique::Auto)
        {
            m_ResolvedAA = DetectRecommendedAA(device.GetPhysicalDevice(),
                                               m_CfgAA.forwardRenderer,
                                               m_CfgAA.supportsMotionVectors);
            VCKLog::Notice("AA",
                std::string("Auto-detected: ") + ToString(m_ResolvedAA));
        }
        else
        {
            VCKLog::Notice("AA",
                std::string("Pinned: ") + ToString(m_ResolvedAA));
        }

        // Step 2: reconcile MSAA sample count with the chosen technique.
        //   Sample-based (MSAA / MSAA_A2C / SampleRate): resolve MSAA_AUTO
        //                                                or respect a pinned
        //                                                value.
        //   Post-process (FXAA / SMAA / TAA / TAAU) + Off: force 1 sample.
        //                                                The post-process
        //                                                pass runs after
        //                                                the main render at
        //                                                1x.
        if (IsSampleBasedAA(m_ResolvedAA))
        {
            if (m_CfgSwapchain.msaaSamples == MSAA_AUTO)
            {
                const VkSampleCountFlagBits picked =
                    DetectRecommendedMSAA(device.GetPhysicalDevice());
                m_CfgSwapchain.msaaSamples = picked;
                VCKLog::Notice("Swapchain",
                    std::string("MSAA auto-detected: ") +
                    std::to_string(static_cast<int>(picked)) + "x");
            }
        }
        else
        {
            if (m_CfgSwapchain.msaaSamples == MSAA_AUTO ||
                m_CfgSwapchain.msaaSamples != VK_SAMPLE_COUNT_1_BIT)
            {
                if (m_CfgSwapchain.msaaSamples != MSAA_AUTO &&
                    m_CfgSwapchain.msaaSamples != VK_SAMPLE_COUNT_1_BIT)
                {
                    // User set an MSAA count but the resolved technique is
                    // post-process - clamp to 1x and warn so the user knows.
                    VCKLog::Warn("Swapchain",
                        std::string("msaaSamples clamped to 1x - technique ") +
                        ToString(m_ResolvedAA) + " runs as post-process");
                }
                m_CfgSwapchain.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
            }
        }

        VCKLog::Info("Swapchain", "Initializing (" +
            std::to_string(width) + "x" + std::to_string(height) + ")...");

        if (!CreateSwapchain(width, height))
        {
            VCKLog::Error("Swapchain", "creation failed");
            return false;
        }

        VCKLog::Notice("Swapchain", "Initialized - " +
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

    bool VulkanSwapchain::Recreate(uint32_t width, uint32_t height, bool drainedExternally)
    {
        // Ignore degenerate sizes (minimized window)
        if (width == 0 || height == 0)
            return true;

        VCKLog::Notice("Swapchain", "Recreating (" +
            std::to_string(width) + "x" + std::to_string(height) + ")");

        // Wait for all in-flight work to complete before touching the swapchain
        // unless the caller has already drained (scheduler-aware path).
        if (!drainedExternally)
            vkDeviceWaitIdle(m_Device->GetDevice());

        DestroySwapchainResources();

        if (!CreateSwapchain(width, height))
        {
            VCKLog::Error("Swapchain", "recreation failed");
            return false;
        }

        VCKLog::Notice("Swapchain", "Recreated - extent " +
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

        // Request one extra image over the minimum by default, or use the
        // caller-provided count (cfg.swapchain.imageCount).  Always clamp to
        // the driver-reported min/max.
        uint32_t imageCount = (m_CfgSwapchain.imageCount != 0)
                                ? m_CfgSwapchain.imageCount
                                : support.Capabilities.minImageCount + 1;
        imageCount = std::max(imageCount, support.Capabilities.minImageCount);
        if (support.Capabilities.maxImageCount > 0)
            imageCount = std::min(imageCount, support.Capabilities.maxImageCount);

        const char* presentModeStr = "FIFO";
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)    presentModeStr = "Mailbox";
        else if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)  presentModeStr = "Immediate";

        VCKLog::Info("Swapchain", std::string("Format: ") + std::to_string(surfaceFormat.format) +
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
            // Two distinct families - images must be shared between them
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
            // Same family - exclusive is faster
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainInfo.queueFamilyIndexCount = 0;
            swapchainInfo.pQueueFamilyIndices = nullptr;
        }

        if (!VK_CHECK(vkCreateSwapchainKHR(m_Device->GetDevice(), &swapchainInfo, nullptr, &m_Swapchain)))
            return false;

        // ── Retrieve images ──────────────────────────────────────────────────────
        uint32_t actualImageCount = 0;
        if (!VK_CHECK(vkGetSwapchainImagesKHR(m_Device->GetDevice(), m_Swapchain, &actualImageCount, nullptr)))
            return false;
        m_Images.resize(actualImageCount);
        if (!VK_CHECK(vkGetSwapchainImagesKHR(m_Device->GetDevice(), m_Swapchain, &actualImageCount, m_Images.data())))
            return false;

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

        // -- MSAA colour targets (one per swapchain image) ---------------------
        // When cfg.swapchain.msaaSamples > 1 we render into a multisampled colour
        // image and the render pass resolves into the single-sample swapchain
        // image.  TRANSIENT_ATTACHMENT_BIT lets tile-based GPUs keep the surface
        // in on-chip memory (zero cost on desktop, big win on mobile).
        if (m_CfgSwapchain.msaaSamples != VK_SAMPLE_COUNT_1_BIT)
        {
            m_MsaaTargets.resize(m_Images.size());
            for (size_t i = 0; i < m_Images.size(); ++i)
            {
                const bool ok = m_MsaaTargets[i].Create(
                    *m_Device,
                    m_Extent.width, m_Extent.height,
                    m_ImageFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    m_CfgSwapchain.msaaSamples);
                if (!ok)
                {
                    VCKLog::Error("Swapchain", "MSAA target[" + std::to_string(i) + "] creation failed");
                    return false;
                }
            }
            VCKLog::Notice("Swapchain", "MSAA " + std::to_string(static_cast<int>(m_CfgSwapchain.msaaSamples)) +
                  "x colour targets created (" + std::to_string(m_Images.size()) + " images)");
        }

        return true;
    }

    void VulkanSwapchain::DestroySwapchainResources()
    {
        if (m_Device == nullptr)
            return;

        VkDevice device = m_Device->GetDevice();

        for (VulkanImage& target : m_MsaaTargets)
            target.Shutdown();
        m_MsaaTargets.clear();

        for (VkImageView view : m_ImageViews)
            vkDestroyImageView(device, view, nullptr);
        m_ImageViews.clear();

        // Images are owned by the swapchain - do NOT call vkDestroyImage on them
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
        // conversion - all vertex colours and textures are stored in display
        // (gamma-encoded) space and are used as-is.
        //
        // Priority:
        //   1. VK_FORMAT_R8G8B8A8_UNORM  - preferred (RGBA, linear write)
        //   2. VK_FORMAT_R8G8B8A8_SRGB   - acceptable (RGBA, sRGB encode)
        //   3. VK_FORMAT_B8G8R8A8_UNORM  - fallback  (BGRA, linear write)
        //   4. VK_FORMAT_B8G8R8A8_SRGB   - last resort

        // Honour an explicit cfg.swapchain.surfaceFormat if set (non-UNDEFINED).
        if (m_CfgSwapchain.surfaceFormat != VK_FORMAT_UNDEFINED)
        {
            for (const VkSurfaceFormatKHR& avail : available)
            {
                if (avail.format == m_CfgSwapchain.surfaceFormat &&
                    avail.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    VCKLog::Info("Swapchain", "Using caller-requested surface format: " +
                          std::to_string(avail.format));
                    return avail;
                }
            }
            VCKLog::Warn("Swapchain", "Requested surface format not supported, falling back to defaults");
        }

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
                    VCKLog::Info("Swapchain", "Selected surface format: " +
                          std::to_string(avail.format));
                    return avail;
                }
            }
        }

        // Last resort: use whatever the driver gives us first.
        VCKLog::Warn("Swapchain", "No preferred surface format found - using fallback: " +
              std::to_string(available[0].format));
        return available[0];
    }

    VkPresentModeKHR VulkanSwapchain::ChoosePresentMode(
        const std::vector<VkPresentModeKHR>& available) const
    {
        // Honour cfg.swapchain.presentMode if the user set one.  Each
        // explicit choice falls back to FIFO when the mode is not supported.
        auto has = [&](VkPresentModeKHR m) {
            for (VkPresentModeKHR avail : available) if (avail == m) return true;
            return false;
        };

        // Present mode is a user-visible choice (affects input latency,
        // tearing, power).  Log with Notice so it shows up even when
        // cfg.debug = false.  Fallback lines use Warn to stand out.
        switch (m_CfgSwapchain.presentMode)
        {
        case PresentMode::Mailbox:
            if (has(VK_PRESENT_MODE_MAILBOX_KHR))   { VCKLog::Notice("Swapchain", "Present mode: Mailbox");   return VK_PRESENT_MODE_MAILBOX_KHR;   }
            VCKLog::Warn("Swapchain", "Mailbox requested but unavailable, falling back to FIFO");
            return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::Immediate:
            if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) { VCKLog::Notice("Swapchain", "Present mode: Immediate"); return VK_PRESENT_MODE_IMMEDIATE_KHR; }
            VCKLog::Warn("Swapchain", "Immediate requested but unavailable, falling back to FIFO");
            return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::Fifo:
            VCKLog::Notice("Swapchain", "Present mode: FIFO (vsync)");
            return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::FifoLatestReady:
            // R24 cfg knob: VK_PRESENT_MODE_FIFO_LATEST_READY_EXT.  When the
            // device + driver advertise it (VK_EXT_present_mode_fifo_latest_ready),
            // pick it; otherwise fall through to plain FIFO with a Notice that
            // tells the user exactly why they didn't get latest-ready behaviour
            // (rule 23 - never silently substitute a present mode).
// Guard on the extension sentinel define, NOT on the present-mode enum
// constant: VK_PRESENT_MODE_FIFO_LATEST_READY_EXT is a C enum value in
// VkPresentModeKHR (no #define exists) so #ifdef on it is dead-code.
// The extension #define lives in vulkan_core.h alongside other ext sentinels.
#ifdef VK_EXT_present_mode_fifo_latest_ready
            if (has(VK_PRESENT_MODE_FIFO_LATEST_READY_EXT))
            {
                VCKLog::Notice("Swapchain",
                    "Present mode: FIFO_LATEST_READY (cfg.swapchain.presentMode, VK_EXT_present_mode_fifo_latest_ready)");
                return VK_PRESENT_MODE_FIFO_LATEST_READY_EXT;
            }
#endif
            VCKLog::Warn("Swapchain",
                "FifoLatestReady requested but VK_EXT_present_mode_fifo_latest_ready unavailable, falling back to FIFO (R23)");
            return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::Auto:
        default:
            if (has(VK_PRESENT_MODE_MAILBOX_KHR))   { VCKLog::Notice("Swapchain", "Present mode: Mailbox (auto)"); return VK_PRESENT_MODE_MAILBOX_KHR; }
            VCKLog::Notice("Swapchain", "Present mode: FIFO (auto, vsync)");
            return VK_PRESENT_MODE_FIFO_KHR;
        }
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

    std::vector<VkImageView> VulkanSwapchain::GetMSAAColorViews() const
    {
        std::vector<VkImageView> out;
        out.reserve(m_MsaaTargets.size());
        for (const VulkanImage& t : m_MsaaTargets)
            out.push_back(t.GetImageView());
        return out;
    }

} // namespace VCK