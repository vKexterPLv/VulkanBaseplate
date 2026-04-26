#pragma once

#include "VulkanHelpers.h"   // VCK::Config, MAX_FRAMES_IN_FLIGHT
#include "VulkanSync.h"
#include <array>
#include <cstdint>

namespace VCK {

    class VulkanDevice;

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanCommand
    //
    //  Owns the graphics command pool and one primary command buffer per frame
    //  slot (MAX_FRAMES_IN_FLIGHT buffers total).
    //
    //  Pool flag: RESET_COMMAND_BUFFER_BIT - each buffer is individually
    //  resettable, so BeginRecording() calls vkResetCommandBuffer instead of
    //  resetting the whole pool.
    //
    //  Usage per frame:
    //    BeginRecording(frameIndex)   - reset + vkBeginCommandBuffer
    //    < record draw commands >
    //    EndRecording(frameIndex)     - vkEndCommandBuffer
    //    < submit via VkQueue >
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanCommand
    {
    public:
        VulkanCommand() = default;
        ~VulkanCommand() = default;

        VulkanCommand(const VulkanCommand&) = delete;
        VulkanCommand& operator=(const VulkanCommand&) = delete;

        // Zero-config form creates 2 slots.  Pass a Config to change framesInFlight.
        bool Initialize(VulkanDevice& device);
        bool Initialize(VulkanDevice& device, const Config& cfg);
        void Shutdown();

        bool BeginRecording(uint32_t frameIndex);
        bool EndRecording(uint32_t frameIndex);

        VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_CommandBuffers[frameIndex]; }
        VkCommandPool   GetCommandPool()                    const { return m_CommandPool; }
        uint32_t        GetFramesInFlight()                 const { return m_FramesInFlight; }

        // ── v0.3: secondary command buffer support ──────────────────────────
        //
        // Secondaries are allocated on demand from the same graphics pool as
        // the per-slot primary buffers.  They can be recorded from worker
        // threads in parallel (Vulkan requires external sync on the *pool*,
        // but the intended usage here is: one thread per secondary with its
        // own short-lived allocation; for multi-threaded recording from one
        // pool the user must serialise AllocateSecondary / FreeSecondary).
        // Rule 18: no internal lock.
        //
        // BeginSecondary accepts a VkCommandBufferInheritanceInfo filled by
        // the caller (render pass / subpass / framebuffer).  ExecuteSecondaries
        // is a thin vkCmdExecuteCommands wrapper exposed for ergonomic
        // symmetry with BeginRecording / EndRecording.
        VkCommandBuffer AllocateSecondary();
        void            FreeSecondary(VkCommandBuffer cb);
        bool            BeginSecondary(VkCommandBuffer                           cb,
                                       const VkCommandBufferInheritanceInfo&     inheritance,
                                       VkCommandBufferUsageFlags                 extraFlags = 0);
        bool            EndSecondary(VkCommandBuffer cb);
        static void     ExecuteSecondaries(VkCommandBuffer primary,
                                           const VkCommandBuffer* secondaries,
                                           uint32_t               count);

    private:
        VulkanDevice* m_Device = nullptr;

        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_CommandBuffers{};
        uint32_t m_FramesInFlight = 2;
    };

} // namespace VCK