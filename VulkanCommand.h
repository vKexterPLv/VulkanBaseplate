#pragma once

#include "VulkanHelpers.h"
#include "VulkanSync.h"   // MAX_FRAMES_IN_FLIGHT
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
    //  Pool flag: RESET_COMMAND_BUFFER_BIT — each buffer is individually
    //  resettable, so BeginRecording() calls vkResetCommandBuffer instead of
    //  resetting the whole pool.
    //
    //  Usage per frame:
    //    BeginRecording(frameIndex)   — reset + vkBeginCommandBuffer
    //    < record draw commands >
    //    EndRecording(frameIndex)     — vkEndCommandBuffer
    //    < submit via VkQueue >
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanCommand
    {
    public:
        VulkanCommand() = default;
        ~VulkanCommand() = default;

        VulkanCommand(const VulkanCommand&) = delete;
        VulkanCommand& operator=(const VulkanCommand&) = delete;

        bool Initialize(VulkanDevice& device);
        void Shutdown();

        bool BeginRecording(uint32_t frameIndex);
        bool EndRecording(uint32_t frameIndex);

        VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_CommandBuffers[frameIndex]; }
        VkCommandPool   GetCommandPool()                    const { return m_CommandPool; }

    private:
        VulkanDevice* m_Device = nullptr;

        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_CommandBuffers{};
    };

} // namespace VCK