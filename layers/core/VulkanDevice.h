#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"   // AMD VMA - place vk_mem_alloc.h in src/render/vulkan/
#include "VulkanHelpers.h"   // VCK::Config

#include <string>
#include <optional>
#include <vector>

namespace VCK {

    class VulkanContext;

    // ─────────────────────────────────────────────────────────────────────────────
    //  Queue family indices resolved during physical device selection.
    //  Both families may be the same index (combined queue) - that is valid.
    // ─────────────────────────────────────────────────────────────────────────────
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> GraphicsFamily;
        std::optional<uint32_t> PresentFamily;
        // v0.3: optional dedicated compute / transfer families.  Absent
        // when the vendor does not expose graphics-less queues; in that
        // case QueueSet::Compute() / Transfer() alias the graphics queue.
        std::optional<uint32_t> ComputeFamily;
        std::optional<uint32_t> TransferFamily;

        bool IsComplete() const
        {
            return GraphicsFamily.has_value() && PresentFamily.has_value();
        }

        bool IsCombined() const
        {
            return IsComplete() && (GraphicsFamily.value() == PresentFamily.value());
        }

        bool HasDedicatedCompute()  const { return ComputeFamily.has_value();  }
        bool HasDedicatedTransfer() const { return TransferFamily.has_value(); }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanDevice
    //
    //  Responsibilities:
    //    1. Enumerate and score physical devices - prefer discrete GPU.
    //    2. Discover queue families (graphics + present).
    //    3. Create VkDevice with required extensions (VK_KHR_swapchain).
    //    4. Retrieve graphics and present queue handles.
    //    5. Create and own the VMA allocator (all buffer/image allocations go here).
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanDevice
    {
    public:
        VulkanDevice() = default;
        ~VulkanDevice() = default;

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // Preferred overload - pulls instance + surface from the context.
        // The Config overloads let the caller control preferDiscreteGpu,
        // extra device extensions, and queue preference.
        bool Initialize(VulkanContext& context);
        bool Initialize(VulkanContext& context, const Config& cfg);
        bool Initialize(VkInstance instance, VkSurfaceKHR surface);
        bool Initialize(VkInstance instance, VkSurfaceKHR surface, const Config& cfg);
        void Shutdown();

        // ── Core accessors ───────────────────────────────────────────────────────
        VkDevice           GetDevice()         const { return m_LogicalDevice; }
        VkPhysicalDevice   GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkQueue            GetGraphicsQueue()  const { return m_GraphicsQueue; }
        VkQueue            GetPresentQueue()   const { return m_PresentQueue; }
        // v0.3: dedicated-queue accessors.  Fall back to the graphics
        // queue when the device did not expose a dedicated family or the
        // caller opted out via Config::DeviceCfg::enableDedicated*Queue.
        VkQueue            GetComputeQueue()   const
        {
            return m_ComputeQueue  != VK_NULL_HANDLE ? m_ComputeQueue  : m_GraphicsQueue;
        }
        VkQueue            GetTransferQueue()  const
        {
            return m_TransferQueue != VK_NULL_HANDLE ? m_TransferQueue : m_GraphicsQueue;
        }
        VmaAllocator       GetAllocator()      const { return m_Allocator; }

        const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

        // True when the VK_KHR_timeline_semaphore feature was requested
        // (Config::DeviceCfg::enableTimelineSemaphores = true) AND the
        // physical device reported support AND we actually enabled it on
        // VkDevice.  Execution-layer TimelineSemaphore / FrameScheduler
        // check this before creating timeline objects.
        bool               HasTimelineSemaphores() const { return m_TimelineSemaphoresEnabled; }

        // ── Swapchain support query (used by VulkanSwapchain) ────────────────────
        struct SwapchainSupportDetails
        {
            VkSurfaceCapabilitiesKHR        Capabilities{};
            std::vector<VkSurfaceFormatKHR> Formats;
            std::vector<VkPresentModeKHR>   PresentModes;
        };

        SwapchainSupportDetails QuerySwapchainSupport(VkSurfaceKHR surface) const;

    private:
        bool PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
        int  ScorePhysicalDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const;
        bool IsPhysicalDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) const;
        bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;

        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;

        bool CreateLogicalDevice();
        bool CreateAllocator(VkInstance instance);

        // ── State ────────────────────────────────────────────────────────────────
        VkPhysicalDevice   m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice           m_LogicalDevice = VK_NULL_HANDLE;
        VkQueue            m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue            m_PresentQueue  = VK_NULL_HANDLE;
        VkQueue            m_ComputeQueue  = VK_NULL_HANDLE;  // v0.3, dedicated only
        VkQueue            m_TransferQueue = VK_NULL_HANDLE;  // v0.3, dedicated only
        VmaAllocator       m_Allocator = VK_NULL_HANDLE;
        QueueFamilyIndices m_QueueFamilyIndices;
        bool               m_TimelineSemaphoresEnabled = false; // v0.3

        // Snapshot of cfg.device (preferDiscreteGpu / extra exts / queue pref).
        Config::DeviceCfg     m_CfgDevice;

        // Snapshot of cfg.rendering (cfg.rendering.mode = Classic / Dynamic).
        // Captured here because the rendering mode toggles which device
        // extension VCK requests (VK_KHR_dynamic_rendering); the actual
        // codepath split lives in VulkanPipeline / Frame in v0.4.
        Config::RenderingCfg  m_CfgRendering;

        // Snapshot of cfg.swapchain.  Only `presentMode` is consulted at
        // device-creation time, to decide whether to request the
        // VK_EXT_present_mode_fifo_latest_ready device extension when the
        // user picks PresentMode::FifoLatestReady.  VulkanSwapchain still
        // owns the full cfg.swapchain on its side (reads its own copy).
        Config::SwapchainCfg  m_CfgSwapchain;

        static constexpr const char* k_RequiredDeviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
    };

} // namespace VCK
