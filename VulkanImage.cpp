#include "VulkanImage.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"

namespace GTA_Sandbox {

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanImage::Create(VulkanDevice& device,
        uint32_t           width,
        uint32_t           height,
        VkFormat           format,
        VkImageUsageFlags  usageFlags,
        VkImageAspectFlags aspectFlags)
    {
        m_Device = &device;
        m_Format = format;
        m_AspectFlags = aspectFlags;

        // ── VkImage via VMA ───────────────────────────────────────────────────────
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;  // GPU-optimal, no linear access
        imageInfo.usage = usageFlags;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult result = vmaCreateImage(
            device.GetAllocator(),
            &imageInfo,
            &allocInfo,
            &m_Image,
            &m_Allocation,
            nullptr);

        if (result != VK_SUCCESS)
        {
            LogVk("VulkanImage::Create — vmaCreateImage failed: " + std::to_string(result));
            return false;
        }

        // ── VkImageView ──────────────────────────────────────────────────────────
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (!VK_CHECK(vkCreateImageView(device.GetDevice(), &viewInfo, nullptr, &m_ImageView)))
        {
            vmaDestroyImage(device.GetAllocator(), m_Image, m_Allocation);
            m_Image = VK_NULL_HANDLE;
            m_Allocation = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanImage::Shutdown()
    {
        if (!m_Device) return;

        if (m_ImageView)
        {
            vkDestroyImageView(m_Device->GetDevice(), m_ImageView, nullptr);
            m_ImageView = VK_NULL_HANDLE;
        }

        if (m_Image)
        {
            vmaDestroyImage(m_Device->GetAllocator(), m_Image, m_Allocation);
            m_Image = VK_NULL_HANDLE;
            m_Allocation = VK_NULL_HANDLE;
        }

        m_Device = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  RecordLayoutTransition
    //
    //  Inserts a VkImageMemoryBarrier.  Access masks and pipeline stage masks are
    //  derived from the old/new layout pair — covering the common transitions:
    //
    //    UNDEFINED              → TRANSFER_DST_OPTIMAL   (before upload)
    //    TRANSFER_DST_OPTIMAL   → SHADER_READ_ONLY_OPTIMAL (after upload)
    //    UNDEFINED              → DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    //    UNDEFINED              → COLOR_ATTACHMENT_OPTIMAL
    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanImage::RecordLayoutTransition(VkCommandBuffer cmd,
        VkImageLayout   oldLayout,
        VkImageLayout   newLayout)
    {
        VkPipelineStageFlags sourceStage = 0;
        VkPipelineStageFlags destinationStage = 0;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_Image;
        barrier.subresourceRange.aspectMask = m_AspectFlags;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        // ── Source access + stage ────────────────────────────────────────────────
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            barrier.srcAccessMask = 0;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else
        {
            // Generic fallback — not optimal, but safe.
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        // ── Destination access + stage ───────────────────────────────────────────
        if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.dstAccessMask =
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            barrier.dstAccessMask = 0;
            destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }
        else
        {
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier(
            cmd,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  RecordCopyFromBuffer
    //  Records a buffer→image blit.  Image must already be in
    //  TRANSFER_DST_OPTIMAL layout (call RecordLayoutTransition first).
    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanImage::RecordCopyFromBuffer(VkCommandBuffer cmd,
        VulkanBuffer& sourceBuffer,
        uint32_t        width,
        uint32_t        height)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = m_AspectFlags;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { width, height, 1 };

        vkCmdCopyBufferToImage(
            cmd,
            sourceBuffer.GetBuffer(),
            m_Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
    }

} // namespace GTA_Sandbox