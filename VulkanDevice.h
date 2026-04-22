#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"   // AMD VMA — place vk_mem_alloc.h in src/render/vulkan/

#include <string>
#include <optional>
#include <vector>

namespace VCK {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Queue family indices resolved during physical device selection.
    //  Both families may be the same index (combined queue) — that is valid.
    // ─────────────────────────────────────────────────────────────────────────────
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> GraphicsFamily;
        std::optional<uint32_t> PresentFamily;

        bool IsComplete() const
        {
            return GraphicsFamily.has_value() && PresentFamily.has_value();
        }

        bool IsCombined() const
        {
            return IsComplete() && (GraphicsFamily.value() == PresentFamily.value());
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanDevice
    //
    //  Responsibilities:
    //    1. Enumerate and score physical devices — prefer discrete GPU.
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

        bool Initialize(VkInstance instance, VkSurfaceKHR surface);
        void Shutdown();

        // ── Core accessors ───────────────────────────────────────────────────────
        VkDevice           GetDevice()         const { return m_LogicalDevice; }
        VkPhysicalDevice   GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkQueue            GetGraphicsQueue()  const { return m_GraphicsQueue; }
        VkQueue            GetPresentQueue()   const { return m_PresentQueue; }
        VmaAllocator       GetAllocator()      const { return m_Allocator; }

        const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

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
        VkQueue            m_PresentQueue = VK_NULL_HANDLE;
        VmaAllocator       m_Allocator = VK_NULL_HANDLE;
        QueueFamilyIndices m_QueueFamilyIndices;

        static constexpr const char* k_RequiredDeviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
    };

} // namespace VCK
