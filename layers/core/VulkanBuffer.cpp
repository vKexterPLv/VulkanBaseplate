#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include <cstring>

namespace VCK {

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanBuffer::Create(VulkanDevice& device,
        VkDeviceSize       size,
        VkBufferUsageFlags usageFlags,
        VmaMemoryUsage     memoryUsage)
    {
        m_Device = &device;
        m_Size = size;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usageFlags;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;

        VkResult result = vmaCreateBuffer(
            device.GetAllocator(),
            &bufferInfo,
            &allocInfo,
            &m_Buffer,
            &m_Allocation,
            nullptr);

        if (result != VK_SUCCESS)
        {
            VCKLog::Error("Buffer", std::string("Create failed - vmaCreateBuffer returned ")
                + std::to_string(result));
            return false;
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanBuffer::Shutdown()
    {
        if (!m_Device) return;

        if (m_Buffer)
        {
            vmaDestroyBuffer(m_Device->GetAllocator(), m_Buffer, m_Allocation);
            m_Buffer = VK_NULL_HANDLE;
            m_Allocation = VK_NULL_HANDLE;
        }

        m_Device = nullptr;
        m_Size = 0;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Named helpers
    // ─────────────────────────────────────────────────────────────────────────────

    // CPU-visible, used as copy source when uploading to GPU-only buffers.
    bool VulkanBuffer::CreateStaging(VulkanDevice& device, VkDeviceSize size)
    {
        return Create(device, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY);
    }

    // GPU-local vertex buffer.  Fill via staging → RecordCopyTo.
    bool VulkanBuffer::CreateVertex(VulkanDevice& device, VkDeviceSize size)
    {
        return Create(device, size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
    }

    // GPU-local index buffer.  Fill via staging → RecordCopyTo.
    bool VulkanBuffer::CreateIndex(VulkanDevice& device, VkDeviceSize size)
    {
        return Create(device, size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
    }

    // CPU→GPU uniform buffer.  Small, frequently updated (e.g. per-frame matrices).
    // CPU_TO_GPU places it in DEVICE_LOCAL | HOST_VISIBLE on integrated GPUs,
    // and in a write-combining host-visible heap on discrete GPUs.
    bool VulkanBuffer::CreateUniform(VulkanDevice& device, VkDeviceSize size)
    {
        return Create(device, size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Upload (host-visible buffers only)
    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanBuffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset)
    {
        void* mapped = nullptr;
        VkResult result = vmaMapMemory(m_Device->GetAllocator(), m_Allocation, &mapped);

        if (result != VK_SUCCESS)
        {
            VCKLog::Error("Buffer", "Upload - vmaMapMemory failed: " + std::to_string(result));
            return false;
        }

        std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, static_cast<size_t>(size));
        vmaUnmapMemory(m_Device->GetAllocator(), m_Allocation);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  RecordCopyTo
    //  Records a VkCmdCopyBuffer into the supplied (already-open) command buffer.
    //  Caller is responsible for appropriate pipeline barriers before/after.
    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanBuffer::RecordCopyTo(VkCommandBuffer cmd,
        VulkanBuffer& destination,
        VkDeviceSize    size,
        VkDeviceSize    srcOffset,
        VkDeviceSize    dstOffset) const
    {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;

        vkCmdCopyBuffer(cmd, m_Buffer, destination.GetBuffer(), 1, &copyRegion);
    }

} // namespace VCK