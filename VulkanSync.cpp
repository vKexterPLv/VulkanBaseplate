#include "VulkanSync.h"
#include "VulkanDevice.h"

namespace GTA_Sandbox {

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanSync::Initialize(VulkanDevice& device)
    {
        m_Device = &device;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        // Start all fences in the signalled state so the first frame doesn't stall
        // waiting on a fence that was never submitted.
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (!VK_CHECK(vkCreateSemaphore(device.GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i])))
                return false;

            if (!VK_CHECK(vkCreateSemaphore(device.GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i])))
                return false;

            if (!VK_CHECK(vkCreateFence(device.GetDevice(), &fenceInfo, nullptr, &m_InFlightFences[i])))
                return false;
        }

        LogVk("VulkanSync initialized — " + std::to_string(MAX_FRAMES_IN_FLIGHT) + " frames in flight");
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanSync::Shutdown()
    {
        if (!m_Device) return;
        VkDevice device = m_Device->GetDevice();

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
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

} // namespace GTA_Sandbox
