#include "VulkanCommand.h"
#include "VulkanDevice.h"

namespace VulkanBaseplate {

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanCommand::Initialize(VulkanDevice& device)
    {
        m_Device = &device;

        // ── Command pool ──────────────────────────────────────────────────────────
        // RESET_COMMAND_BUFFER_BIT: individual buffers can be reset without
        // affecting the others.  Tied to the graphics queue family.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = device.GetQueueFamilyIndices().GraphicsFamily.value();

        if (!VK_CHECK(vkCreateCommandPool(device.GetDevice(), &poolInfo, nullptr, &m_CommandPool)))
            return false;

        // ── Command buffers ───────────────────────────────────────────────────────
        // One primary buffer per frame-in-flight slot.
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        if (!VK_CHECK(vkAllocateCommandBuffers(device.GetDevice(), &allocInfo, m_CommandBuffers.data())))
            return false;

        LogVk("VulkanCommand initialized — pool + "
            + std::to_string(MAX_FRAMES_IN_FLIGHT) + " command buffers");
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanCommand::Shutdown()
    {
        if (!m_Device) return;

        // Command buffers are implicitly freed when the pool is destroyed.
        if (m_CommandPool)
        {
            vkDestroyCommandPool(m_Device->GetDevice(), m_CommandPool, nullptr);
            m_CommandPool = VK_NULL_HANDLE;
        }

        m_Device = nullptr;
        LogVk("VulkanCommand shut down");
    }

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanCommand::BeginRecording(uint32_t frameIndex)
    {
        VkCommandBuffer cmd = m_CommandBuffers[frameIndex];

        // Reset the individual buffer — pool stays intact, other slots unaffected.
        if (!VK_CHECK(vkResetCommandBuffer(cmd, 0)))
            return false;

        // ONE_TIME_SUBMIT: hint to the driver that this buffer is recorded and
        // submitted exactly once before the next reset.
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        return VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanCommand::EndRecording(uint32_t frameIndex)
    {
        return VK_CHECK(vkEndCommandBuffer(m_CommandBuffers[frameIndex]));
    }

} // namespace VulkanBaseplate
