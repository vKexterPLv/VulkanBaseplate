#pragma once

#include "VulkanHelpers.h"
#include "vk_mem_alloc.h"
#include <cstdint>

namespace GTA_Sandbox {

    class VulkanDevice;
    class VulkanBuffer;

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanImage
    //
    //  Wraps a VkImage + VkImageView + VmaAllocation.
    //  All images are TILING_OPTIMAL (device-local).
    //
    //  Texture upload workflow:
    //    1. image.Create(device, w, h, VK_FORMAT_R8G8B8A8_SRGB,
    //                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    //                   VK_IMAGE_ASPECT_COLOR_BIT)
    //    2. image.RecordLayoutTransition(cmd,
    //                                    VK_IMAGE_LAYOUT_UNDEFINED,
    //                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    //    3. image.RecordCopyFromBuffer(cmd, stagingBuffer, width, height)
    //    4. image.RecordLayoutTransition(cmd,
    //                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    //    5. Submit cmd, wait, staging.Shutdown()
    //
    //  For depth/stencil images: supply VK_IMAGE_ASPECT_DEPTH_BIT (etc.) and
    //  the matching VK_FORMAT_D*.
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanImage
    {
    public:
        VulkanImage() = default;
        ~VulkanImage() = default;

        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;

        // ── Creation ─────────────────────────────────────────────────────────────
        bool Create(VulkanDevice& device,
            uint32_t           width,
            uint32_t           height,
            VkFormat           format,
            VkImageUsageFlags  usageFlags,
            VkImageAspectFlags aspectFlags);

        void Shutdown();

        // ── Command recording ────────────────────────────────────────────────────
        // Records a pipeline barrier that transitions the image layout.
        // Must be called within an open command buffer.
        void RecordLayoutTransition(VkCommandBuffer cmd,
            VkImageLayout   oldLayout,
            VkImageLayout   newLayout);

        // Records a buffer→image copy (buffer must be TRANSFER_SRC).
        // Image must already be in TRANSFER_DST_OPTIMAL layout.
        void RecordCopyFromBuffer(VkCommandBuffer cmd,
            VulkanBuffer& sourceBuffer,
            uint32_t        width,
            uint32_t        height);

        // ── Accessors ────────────────────────────────────────────────────────────
        VkImage     GetImage()     const { return m_Image; }
        VkImageView GetImageView() const { return m_ImageView; }
        VkFormat    GetFormat()    const { return m_Format; }
        bool        IsValid()      const { return m_Image != VK_NULL_HANDLE; }

    private:
        VulkanDevice* m_Device = nullptr;
        VkImage            m_Image = VK_NULL_HANDLE;
        VkImageView        m_ImageView = VK_NULL_HANDLE;
        VmaAllocation      m_Allocation = VK_NULL_HANDLE;
        VkFormat           m_Format = VK_FORMAT_UNDEFINED;
        VkImageAspectFlags m_AspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    };

} // namespace GTA_Sandbox