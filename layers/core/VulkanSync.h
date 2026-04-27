#pragma once

#include "VulkanHelpers.h"   // VCK::Config, MAX_FRAMES_IN_FLIGHT
#include <array>
#include <cstdint>

namespace VCK {

    class VulkanDevice;

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanSync
    //
    //  Owns all per-frame synchronisation primitives:
    //    • imageAvailableSemaphore  - signals when vkAcquireNextImageKHR is done
    //    • renderFinishedSemaphore  - signals when the submitted command buffer is done
    //    • inFlightFence            - CPU waits here before reusing the frame slot
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

        // Zero-config form creates 2 slots.  Pass a Config to change framesInFlight.
        bool Initialize(VulkanDevice& device);
        bool Initialize(VulkanDevice& device, const Config& cfg);
        void Shutdown();

        // ── Per-frame accessors ───────────────────────────────────────────────────
        VkSemaphore GetImageAvailableSemaphore(uint32_t frameIndex) const { return m_ImageAvailableSemaphores[frameIndex]; }
        VkSemaphore GetRenderFinishedSemaphore(uint32_t frameIndex) const { return m_RenderFinishedSemaphores[frameIndex]; }
        VkFence     GetInFlightFence(uint32_t frameIndex) const { return m_InFlightFences[frameIndex]; }

        // ── Frame counter ─────────────────────────────────────────────────────────
        uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
        uint32_t GetFramesInFlight()    const { return m_FramesInFlight; }
        void     AdvanceFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % m_FramesInFlight; }

    private:
        VulkanDevice* m_Device = nullptr;

        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_ImageAvailableSemaphores{};
        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_RenderFinishedSemaphores{};
        std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_InFlightFences{};

        uint32_t m_FramesInFlight = 2; // clamped to MAX_FRAMES_IN_FLIGHT by Initialize
        uint32_t m_CurrentFrame = 0;
    };

} // namespace VCK