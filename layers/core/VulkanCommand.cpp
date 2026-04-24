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

        // Clamp requested count into [1, MAX_FRAMES_IN_FLIGHT].  Loud so
        // it matches VulkanSync's clamp log - rule 14 (fail fast, fail loud).
        uint32_t requested = cfg.sync.framesInFlight;
        if (requested == 0) requested = 1;
        if (requested > MAX_FRAMES_IN_FLIGHT) {
            VCKLog::Warn("Command", "framesInFlight=" + std::to_string(requested) +
                  " exceeds MAX_FRAMES_IN_FLIGHT=" + std::to_string(MAX_FRAMES_IN_FLIGHT) +
                  ", clamping");
            requested = MAX_FRAMES_IN_FLIGHT;
        }
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

        VCKLog::Info("Command", std::string("Initialized - pool + ")
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
        VCKLog::Info("Command", "Shut down");
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

    // ─────────────────────────────────────────────────────────────────────────────
    //  Secondary command buffers  (v0.3)
    // ─────────────────────────────────────────────────────────────────────────────
    VkCommandBuffer VulkanCommand::AllocateSecondary()
    {
        if (m_Device == nullptr || m_CommandPool == VK_NULL_HANDLE)
            return VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_CommandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (!VK_CHECK(vkAllocateCommandBuffers(m_Device->GetDevice(), &allocInfo, &cb)))
            return VK_NULL_HANDLE;
        return cb;
    }

    void VulkanCommand::FreeSecondary(VkCommandBuffer cb)
    {
        if (m_Device == nullptr || m_CommandPool == VK_NULL_HANDLE || cb == VK_NULL_HANDLE)
            return;
        vkFreeCommandBuffers(m_Device->GetDevice(), m_CommandPool, 1, &cb);
    }

    bool VulkanCommand::BeginSecondary(VkCommandBuffer                           cb,
                                       const VkCommandBufferInheritanceInfo&     inheritance,
                                       VkCommandBufferUsageFlags                 extraFlags)
    {
        VkCommandBufferBeginInfo bi{};
        bi.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        // RENDER_PASS_CONTINUE is required for secondaries that will be
        // executed inside a render pass.  Callers may pass 0 for extraFlags
        // to get only the continue bit (the common case) or add
        // ONE_TIME_SUBMIT / SIMULTANEOUS_USE to match their usage.
        bi.flags            = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | extraFlags;
        bi.pInheritanceInfo = &inheritance;
        return VK_CHECK(vkBeginCommandBuffer(cb, &bi));
    }

    bool VulkanCommand::EndSecondary(VkCommandBuffer cb)
    {
        return VK_CHECK(vkEndCommandBuffer(cb));
    }

    void VulkanCommand::ExecuteSecondaries(VkCommandBuffer         primary,
                                           const VkCommandBuffer*  secondaries,
                                           uint32_t                count)
    {
        if (primary == VK_NULL_HANDLE || secondaries == nullptr || count == 0) return;
        vkCmdExecuteCommands(primary, count, secondaries);
    }

} // namespace VCK
