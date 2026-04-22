#pragma once

#include "VulkanHelpers.h"
#include "vk_mem_alloc.h"
#include <cstdint>

namespace VCK {

    class VulkanDevice;

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanBuffer
    //
    //  Generic VkBuffer + VmaAllocation wrapper.
    //
    //  Low-level:
    //    Create(device, size, usageFlags, vmaMemoryUsage)
    //
    //  Helpers for common patterns:
    //    CreateStaging    — CPU-visible TRANSFER_SRC, mapped/unmapped on demand
    //    CreateVertex     — GPU-only VERTEX_BUFFER | TRANSFER_DST
    //    CreateIndex      — GPU-only INDEX_BUFFER  | TRANSFER_DST
    //    CreateUniform    — CPU→GPU UNIFORM_BUFFER, persistently mapped
    //
    //  Upload workflow for GPU-only buffers:
    //    1. CreateStaging → staging.Upload(data, size)
    //    2. CreateVertex / CreateIndex → device-local
    //    3. staging.RecordCopyTo(cmd, dst, size)   ← record into a command buffer
    //    4. Submit cmd, wait, staging.Shutdown()
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanBuffer
    {
    public:
        VulkanBuffer() = default;
        ~VulkanBuffer() = default;

        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;

        // ── Generic creation ─────────────────────────────────────────────────────
        bool Create(VulkanDevice& device,
            VkDeviceSize         size,
            VkBufferUsageFlags   usageFlags,
            VmaMemoryUsage       memoryUsage);

        void Shutdown();

        // ── Named helpers ────────────────────────────────────────────────────────
        bool CreateStaging(VulkanDevice& device, VkDeviceSize size);
        bool CreateVertex(VulkanDevice& device, VkDeviceSize size);
        bool CreateIndex(VulkanDevice& device, VkDeviceSize size);
        bool CreateUniform(VulkanDevice& device, VkDeviceSize size);

        // ── Data upload (host-visible buffers only) ───────────────────────────────
        // Maps, copies, unmaps.  Not valid on GPU_ONLY buffers.
        bool Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

        // ── Command recording ────────────────────────────────────────────────────
        // Records a buffer→buffer copy into an already-open command buffer.
        void RecordCopyTo(VkCommandBuffer cmd,
            VulkanBuffer& destination,
            VkDeviceSize    size,
            VkDeviceSize    srcOffset = 0,
            VkDeviceSize    dstOffset = 0) const;

        // ── Accessors ────────────────────────────────────────────────────────────
        VkBuffer     GetBuffer() const { return m_Buffer; }
        VkDeviceSize GetSize()   const { return m_Size; }
        bool         IsValid()   const { return m_Buffer != VK_NULL_HANDLE; }

    private:
        VulkanDevice* m_Device = nullptr;
        VkBuffer      m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkDeviceSize  m_Size = 0;
    };

} // namespace VCK