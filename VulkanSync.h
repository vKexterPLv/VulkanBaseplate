#pragma once

#include "VulkanHelpers.h"
#include <array>
#include <cstdint>

namespace GTA_Sandbox {

    class VulkanDevice;

    // ─────────────────────────────────────────────────────────────────────────────
    //  How many CPU frames may be recorded / submitted while the GPU is still
    //  processing a previous one.  3 swapchain images, 2 frames in flight.
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanSync
    //
    //  Owns all per-frame synchronisation primitives:
    //    • imageAvailableSemaphore  — signals when vkAcquireNextImageKHR is done
    //    • renderFinishedSemaphore  — signals when the submitted command buffer is done
    //    • inFlightFence            — CPU waits here before reusing the frame slot
    //
    //  All fences are created pre-signalled so the very first frame doesn't block.
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanSync
    {
    public:
        VulkanSync() = default;
        ~VulkanSync() = default;

        VulkanSync(const VulkanSync&) = delete;
        VulkanSync& operator=(const VulkanSync&) = delete;

        bool Initialize(VulkanDevice& device);
        void Shutdown();

        // ── Per-frame accessors ───────────────────────────────────────────────────
        VkSemaphore GetImageAvailableSemaphore(uint32_t frameIndex) const { return m_ImageAvailableSemaphores[frameIndex]; }
        VkSemaphore GetRenderFinishedSemaphore(uint32_t frameIndex) const { return m_RenderFinishedSemaphores[frameIndex]; }
        VkFence     GetInFlightFence(uint32_t frameIndex) const { return m_InFlightFences[frameIndex]; }

        // ── Frame counter ─────────────────────────────────────────────────────────
        uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
        void     AdvanceFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

    private:
        VulkanDevice* m_Device = nullptr;

        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_ImageAvailableSemaphores{};
        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_RenderFinishedSemaphores{};
        std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_InFlightFences{};

        uint32_t m_CurrentFrame = 0;
    };

} // namespace GTA_Sandbox