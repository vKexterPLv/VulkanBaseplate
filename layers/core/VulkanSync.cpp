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
            VCKLog::Warn("Sync", "framesInFlight " + std::to_string(requested) +
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

        // Inlined cleanup helper: destroys every primitive created in slots
        // [0, builtSlots) plus any partial slot at index `builtSlots` whose
        // imageAvailable / renderFinished have been written.  Stays inline
        // (rather than calling Shutdown()) so a failed Initialize never
        // logs a misleading "Sync Shut down" Info line.
        const auto rollback = [&](uint32_t builtSlots,
                                  bool partialImageAvailable,
                                  bool partialRenderFinished) {
            for (uint32_t k = 0; k < builtSlots; ++k) {
                vkDestroyFence(device.GetDevice(), m_InFlightFences[k], nullptr);
                vkDestroySemaphore(device.GetDevice(), m_RenderFinishedSemaphores[k], nullptr);
                vkDestroySemaphore(device.GetDevice(), m_ImageAvailableSemaphores[k], nullptr);
                m_InFlightFences[k]            = VK_NULL_HANDLE;
                m_RenderFinishedSemaphores[k]  = VK_NULL_HANDLE;
                m_ImageAvailableSemaphores[k]  = VK_NULL_HANDLE;
            }
            if (partialRenderFinished) {
                vkDestroySemaphore(device.GetDevice(), m_RenderFinishedSemaphores[builtSlots], nullptr);
                m_RenderFinishedSemaphores[builtSlots] = VK_NULL_HANDLE;
            }
            if (partialImageAvailable) {
                vkDestroySemaphore(device.GetDevice(), m_ImageAvailableSemaphores[builtSlots], nullptr);
                m_ImageAvailableSemaphores[builtSlots] = VK_NULL_HANDLE;
            }
            m_Device         = nullptr;
            m_FramesInFlight = 0;
            m_CurrentFrame   = 0;
        };

        for (uint32_t i = 0; i < m_FramesInFlight; ++i)
        {
            if (!VK_OK(vkCreateSemaphore(device.GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i])))
            {
                VCKLog::Error("Sync",
                    "vkCreateSemaphore failed (imageAvailable, frame " + std::to_string(i) + ")");
                rollback(i, false, false);
                return false;
            }

            if (!VK_OK(vkCreateSemaphore(device.GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i])))
            {
                VCKLog::Error("Sync",
                    "vkCreateSemaphore failed (renderFinished, frame " + std::to_string(i) + ")");
                rollback(i, true, false);
                return false;
            }

            if (!VK_OK(vkCreateFence(device.GetDevice(), &fenceInfo, nullptr, &m_InFlightFences[i])))
            {
                VCKLog::Error("Sync",
                    "vkCreateFence failed (inFlight, frame " + std::to_string(i) + ")");
                rollback(i, true, true);
                return false;
            }
        }

        VCKLog::Info("Sync", "Initialized - " + std::to_string(m_FramesInFlight) + " frames in flight");
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
        VCKLog::Info("Sync", "Shut down");
    }

} // namespace VCK
