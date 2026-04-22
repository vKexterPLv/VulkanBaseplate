#include "VulkanCommand.h"
#include "VulkanDevice.h"

namespace VCK {

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanCommand::Initialize(VulkanDevice& device)
    {
        // Zero-config path: default Config gives framesInFlight=2.
        Config cfg;
        return Initialize(device, cfg);
    }

    bool VulkanCommand::Initialize(VulkanDevice& device, const Config& cfg)
    {
        m_Device = &device;

        // Clamp requested count into [1, MAX_FRAMES_IN_FLIGHT].
        uint32_t requested = cfg.sync.framesInFlight;
        if (requested == 0) requested = 1;
        if (requested > MAX_FRAMES_IN_FLIGHT)
            requested = MAX_FRAMES_IN_FLIGHT;
        m_FramesInFlight = requested;

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
        allocInfo.commandBufferCount = m_FramesInFlight;

        if (!VK_CHECK(vkAllocateCommandBuffers(device.GetDevice(), &allocInfo, m_CommandBuffers.data())))
            return false;

        LogVk("VulkanCommand initialized - pool + "
            + std::to_string(m_FramesInFlight) + " command buffers");
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

        // Reset the individual buffer - pool stays intact, other slots unaffected.
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

} // namespace VCK
