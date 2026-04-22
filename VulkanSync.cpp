#include "VulkanSync.h"
#include "VulkanDevice.h"

namespace VCK {

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanSync::Initialize(VulkanDevice& device)
    {
        // Zero-config path: default Config gives framesInFlight=2.
        Config cfg;
        return Initialize(device, cfg);
    }

    bool VulkanSync::Initialize(VulkanDevice& device, const Config& cfg)
    {
        m_Device = &device;

        // Clamp requested count into [1, MAX_FRAMES_IN_FLIGHT].
        uint32_t requested = cfg.sync.framesInFlight;
        if (requested == 0) requested = 1;
        if (requested > MAX_FRAMES_IN_FLIGHT)
        {
            LogVk("[VulkanSync] framesInFlight " + std::to_string(requested) +
                  " exceeds MAX_FRAMES_IN_FLIGHT (" + std::to_string(MAX_FRAMES_IN_FLIGHT) +
                  "), clamping");
            requested = MAX_FRAMES_IN_FLIGHT;
        }
        m_FramesInFlight = requested;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        // Start all fences in the signalled state so the first frame doesn't stall
        // waiting on a fence that was never submitted.
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < m_FramesInFlight; ++i)
        {
            if (!VK_CHECK(vkCreateSemaphore(device.GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i])))
                return false;

            if (!VK_CHECK(vkCreateSemaphore(device.GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i])))
                return false;

            if (!VK_CHECK(vkCreateFence(device.GetDevice(), &fenceInfo, nullptr, &m_InFlightFences[i])))
                return false;
        }

        LogVk("VulkanSync initialized — " + std::to_string(m_FramesInFlight) + " frames in flight");
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanSync::Shutdown()
    {
        if (!m_Device) return;
        VkDevice device = m_Device->GetDevice();

        for (uint32_t i = 0; i < m_FramesInFlight; ++i)
        {
            // Destroy in reverse creation order: fence → semaphores
            if (m_InFlightFences[i])
                vkDestroyFence(device, m_InFlightFences[i], nullptr);

            if (m_RenderFinishedSemaphores[i])
                vkDestroySemaphore(device, m_RenderFinishedSemaphores[i], nullptr);

            if (m_ImageAvailableSemaphores[i])
                vkDestroySemaphore(device, m_ImageAvailableSemaphores[i], nullptr);
        }

        m_Device = nullptr;
        m_CurrentFrame = 0;
        LogVk("VulkanSync shut down");
    }

} // namespace VCK
