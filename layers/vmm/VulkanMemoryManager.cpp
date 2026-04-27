#include "VulkanMemoryManager.h"
#include <cassert>
#include <algorithm>

namespace VCK {


// =============================================================================
//  LAYER 1 - VmmRawAlloc
// =============================================================================

// -----------------------------------------------------------------------------
//  CreateBuffer - generic
// -----------------------------------------------------------------------------
VmmBuffer VmmRawAlloc::CreateBuffer(VulkanDevice&      device,
                                    VkDeviceSize       size,
                                    VkBufferUsageFlags usage,
                                    VmaMemoryUsage     memUsage,
                                    bool               persistentMap)
{
    VmmBuffer result;
    result.size = size;

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = memUsage;
    if (persistentMap)
        aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    VkResult r = vmaCreateBuffer(device.GetAllocator(), &bci, &aci,
                                 &result.buffer, &result.alloc, &allocInfo);
    if (r != VK_SUCCESS)
    {
        VCKLog::Error("VMM", "VmmRawAlloc::CreateBuffer failed: " + std::to_string(r));
        return result;
    }

    if (persistentMap)
        result.mapped = allocInfo.pMappedData;

    return result;
}

// -----------------------------------------------------------------------------
//  Named convenience variants
// -----------------------------------------------------------------------------
VmmBuffer VmmRawAlloc::CreateStaging(VulkanDevice& device, VkDeviceSize size)
{
    return CreateBuffer(device, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY,
        true);   // persistently mapped - avoids map/unmap per upload
}

VmmBuffer VmmRawAlloc::CreateVertex(VulkanDevice& device, VkDeviceSize size)
{
    return CreateBuffer(device, size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
}

VmmBuffer VmmRawAlloc::CreateIndex(VulkanDevice& device, VkDeviceSize size)
{
    return CreateBuffer(device, size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
}

VmmBuffer VmmRawAlloc::CreateUniform(VulkanDevice& device, VkDeviceSize size)
{
    return CreateBuffer(device, size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        true);   // persistently mapped - Write() without map/unmap overhead
}

// -----------------------------------------------------------------------------
//  CreateImage - generic
// -----------------------------------------------------------------------------
VmmImage VmmRawAlloc::CreateImage(VulkanDevice&      device,
                                  uint32_t           width,
                                  uint32_t           height,
                                  VkFormat           format,
                                  VkImageUsageFlags  usage,
                                  VkImageAspectFlags aspect,
                                  uint32_t           mipLevels)
{
    VmmImage result;
    result.width     = width;
    result.height    = height;
    result.format    = format;
    result.mipLevels = mipLevels;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = { width, height, 1 };
    ici.mipLevels     = mipLevels;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkResult r = vmaCreateImage(device.GetAllocator(), &ici, &aci,
                                &result.image, &result.alloc, nullptr);
    if (r != VK_SUCCESS)
    {
        VCKLog::Error("VMM", "VmmRawAlloc::CreateImage failed: " + std::to_string(r));
        return result;
    }

    // Build VkImageView spanning all mip levels
    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = result.image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = format;
    vci.subresourceRange.aspectMask     = aspect;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = mipLevels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device.GetDevice(), &vci, nullptr, &result.view) != VK_SUCCESS)
    {
        VCKLog::Error("VMM", "VmmRawAlloc::CreateImage - vkCreateImageView failed");
        vmaDestroyImage(device.GetAllocator(), result.image, result.alloc);
        result.image = VK_NULL_HANDLE;
        result.alloc = VK_NULL_HANDLE;
    }

    return result;
}

// -----------------------------------------------------------------------------
//  Free
// -----------------------------------------------------------------------------
void VmmRawAlloc::FreeBuffer(VulkanDevice& device, VmmBuffer& buf)
{
    if (buf.buffer)
    {
        vmaDestroyBuffer(device.GetAllocator(), buf.buffer, buf.alloc);
        buf.buffer = VK_NULL_HANDLE;
        buf.alloc  = VK_NULL_HANDLE;
        buf.mapped = nullptr;
        buf.size   = 0;
    }
}

void VmmRawAlloc::FreeImage(VulkanDevice& device, VmmImage& img)
{
    if (img.view)  { vkDestroyImageView(device.GetDevice(), img.view, nullptr); img.view = VK_NULL_HANDLE; }
    if (img.image) { vmaDestroyImage(device.GetAllocator(), img.image, img.alloc);
                     img.image = VK_NULL_HANDLE; img.alloc = VK_NULL_HANDLE; }
}


// =============================================================================
//  LAYER 2 - VmmRegistry
// =============================================================================

void VmmRegistry::Initialize(VulkanDevice& device)
{
    m_Device = &device;
    m_NextId = 1;
}

void VmmRegistry::Shutdown()
{
    FreeAll();
    m_Device = nullptr;
}

// -----------------------------------------------------------------------------
//  Register / Unregister
//
//  Register takes its argument by reference and stamps the fresh id onto the
//  caller's handle so the caller no longer has to do `buf.id = Register(...)`.
//  The registry stores its own copy (with the id already set).
// -----------------------------------------------------------------------------
uint32_t VmmRegistry::Register(VmmBuffer& buf, ResourceInfo info)
{
    uint32_t id = m_NextId++;
    buf.id = id;
    m_Buffers[id] = { buf, info };
    return id;
}

uint32_t VmmRegistry::Register(VmmImage& img, ResourceInfo info)
{
    uint32_t id = m_NextId++;
    img.id = id;
    m_Images[id] = { img, info };
    return id;
}

void VmmRegistry::UnregisterBuffer(uint32_t id)
{
    if (id != 0)
        m_Buffers.erase(id);
}

void VmmRegistry::UnregisterImage(uint32_t id)
{
    if (id != 0)
        m_Images.erase(id);
}

// -----------------------------------------------------------------------------
//  Lookup
// -----------------------------------------------------------------------------
VmmBuffer* VmmRegistry::FindBuffer(uint32_t id)
{
    auto it = m_Buffers.find(id);
    return it != m_Buffers.end() ? &it->second.buf : nullptr;
}

VmmImage* VmmRegistry::FindImage(uint32_t id)
{
    auto it = m_Images.find(id);
    return it != m_Images.end() ? &it->second.img : nullptr;
}

// -----------------------------------------------------------------------------
//  FreeTransient - called at BeginFrame to retire the previous cycle's scratch
// -----------------------------------------------------------------------------
void VmmRegistry::FreeTransient(uint32_t frameSlot)
{
    // Collect IDs first to avoid iterator invalidation
    std::vector<uint32_t> toFree;

    for (auto& [id, entry] : m_Buffers)
    {
        if (entry.info.lifetime == Lifetime::TransientFrame &&
            entry.info.frameSlot == frameSlot)
            toFree.push_back(id);
    }

    for (uint32_t id : toFree)
    {
        auto& entry = m_Buffers[id];
        // Every registered TransientFrame entry is a stand-alone overflow
        // allocation - pure sub-allocations are NOT registered (they have no
        // independent lifetime).  So unconditionally free here.
        VmmRawAlloc::FreeBuffer(*m_Device, entry.buf);
        m_Buffers.erase(id);
    }

    // Images are never transient in the current design (no use case for it),
    // but the hook is here if needed.
}

// -----------------------------------------------------------------------------
//  FreeAll - frees Persistent + FrameBuffered; Manual entries left to caller
// -----------------------------------------------------------------------------
void VmmRegistry::FreeAll()
{
    for (auto& [id, entry] : m_Buffers)
    {
        if (entry.info.lifetime != Lifetime::Manual)
            VmmRawAlloc::FreeBuffer(*m_Device, entry.buf);
    }
    m_Buffers.clear();

    for (auto& [id, entry] : m_Images)
    {
        if (entry.info.lifetime != Lifetime::Manual)
            VmmRawAlloc::FreeImage(*m_Device, entry.img);
    }
    m_Images.clear();
}

// -----------------------------------------------------------------------------
//  LogStats
// -----------------------------------------------------------------------------
void VmmRegistry::LogStats() const
{
    VCKLog::Info("VMM", "Registry - " +
          std::to_string(m_Buffers.size()) + " buffers, " +
          std::to_string(m_Images.size())  + " images tracked");

    for (const auto& [id, entry] : m_Buffers)
    {
        const char* lt =
            entry.info.lifetime == Lifetime::TransientFrame ? "TransientFrame" :
            entry.info.lifetime == Lifetime::FrameBuffered  ? "FrameBuffered"  :
            entry.info.lifetime == Lifetime::Persistent     ? "Persistent"     :
                                                              "Manual";
        VCKLog::Info("VMM", "  [buf " + std::to_string(id) + "] " +
              std::string(entry.info.debugName ? entry.info.debugName : "(unnamed)") +
              "  " + lt +
              "  size=" + std::to_string(entry.buf.size));
    }

    for (const auto& [id, entry] : m_Images)
    {
        const char* lt =
            entry.info.lifetime == Lifetime::Persistent ? "Persistent" : "Manual";
        VCKLog::Info("VMM", "  [img " + std::to_string(id) + "] " +
              std::string(entry.info.debugName ? entry.info.debugName : "(unnamed)") +
              "  " + lt +
              "  " + std::to_string(entry.img.width) + "x" + std::to_string(entry.img.height));
    }
}


// =============================================================================
//  LAYER 3 - VulkanMemoryManager
// =============================================================================

// -----------------------------------------------------------------------------
//  StagingRing helpers
// -----------------------------------------------------------------------------

bool VulkanMemoryManager::StagingRing::HasSpace(VkDeviceSize needed) const
{
    // Ring is treated as linear within a frame: we don't wrap mid-frame.
    // If the write head plus needed exceeds capacity we're full.
    return (writeHead + needed) <= capacity;
}

VkDeviceSize VulkanMemoryManager::StagingRing::Claim(VkDeviceSize size, VkDeviceSize alignment)
{
    // Align the write head
    VkDeviceSize aligned = (writeHead + alignment - 1) & ~(alignment - 1);
    writeHead = aligned + size;
    inFlight += size;
    return aligned;
}

void VulkanMemoryManager::StagingRing::Reset()
{
    writeHead = 0;
    inFlight  = 0;
    frameTails.fill(0);
}

// -----------------------------------------------------------------------------
//  TransientBlock helpers
// -----------------------------------------------------------------------------

bool VulkanMemoryManager::TransientBlock::HasSpace(VkDeviceSize needed,
                                                    VkDeviceSize alignment) const
{
    VkDeviceSize aligned = (offset + alignment - 1) & ~(alignment - 1);
    return (aligned + needed) <= capacity;
}

VkDeviceSize VulkanMemoryManager::TransientBlock::Claim(VkDeviceSize size,
                                                         VkDeviceSize alignment)
{
    VkDeviceSize aligned = (offset + alignment - 1) & ~(alignment - 1);
    offset = aligned + size;
    return aligned;
}

// -----------------------------------------------------------------------------
//  Initialize
// -----------------------------------------------------------------------------
bool VulkanMemoryManager::Initialize(VulkanDevice& device,
                                     VulkanCommand& command,
                                     const Config& config)
{
    m_Device  = &device;
    m_Command = &command;
    m_Config  = config;

    m_Registry.Initialize(device);

    // ── Staging command pool (bound to transfer family) ───────────────────────
    // v0.3: VMM owns its own VkCommandPool tied to the transfer queue family
    // that SubmitStagingCmd() will submit to.  Required by
    // VUID-vkQueueSubmit-pCommandBuffers-00074 whenever the transfer family
    // differs from the graphics family (dedicated transfer on AMD/NVIDIA).
    // When transfer aliases graphics (Intel iGPU etc.) the pool is still
    // correct - it's just on the same family.
    const QueueFamilyIndices& qfi = device.GetQueueFamilyIndices();
    m_GraphicsFamily = qfi.GraphicsFamily.value_or(0);
    m_TransferFamily = qfi.TransferFamily.value_or(m_GraphicsFamily);

    VkCommandPoolCreateInfo tpci{};
    tpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    tpci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    tpci.queueFamilyIndex = m_TransferFamily;

    if (!VK_CHECK(vkCreateCommandPool(device.GetDevice(), &tpci, nullptr, &m_TransferPool)))
    {
        VCKLog::Error("VMM", "Failed to create transfer command pool (family=" +
              std::to_string(m_TransferFamily) + ")");
        return false;
    }

    VCKLog::Info("VMM", "Transfer command pool bound to family " +
          std::to_string(m_TransferFamily) +
          (qfi.HasDedicatedTransfer() ? " (dedicated)" : " (aliased to graphics)"));

    // ── Staging ring ──────────────────────────────────────────────────────────
    m_Ring.buffer   = VmmRawAlloc::CreateStaging(device, config.stagingRingSize);
    m_Ring.capacity = config.stagingRingSize;
    m_Ring.Reset();

    if (!m_Ring.buffer.IsValid())
    {
        VCKLog::Error("VMM", "Failed to allocate staging ring (" +
              std::to_string(config.stagingRingSize / (1024*1024)) + " MB)");
        return false;
    }

    VCKLog::Info("VMM", "Staging ring " +
          std::to_string(config.stagingRingSize / (1024*1024)) + " MB allocated");

    // ── Transient blocks (one per frame slot) ─────────────────────────────────
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        // Transient buffers need to be usable as vertex, index, and uniform
        // buffers so a single block covers all dynamic-geometry use cases.
        m_Transient[i].buffer = VmmRawAlloc::CreateBuffer(
            device,
            config.transientBlockSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT  |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT   |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            true);   // persistently mapped

        m_Transient[i].capacity = config.transientBlockSize;
        m_Transient[i].offset   = 0;

        if (!m_Transient[i].buffer.IsValid())
        {
            VCKLog::Error("VMM", "Failed to allocate transient block for slot " + std::to_string(i));
            return false;
        }
    }

    VCKLog::Info("VMM", std::to_string(MAX_FRAMES_IN_FLIGHT) + " transient blocks x " +
          std::to_string(config.transientBlockSize / (1024*1024)) + " MB allocated");

    return true;
}

// -----------------------------------------------------------------------------
//  Shutdown
// -----------------------------------------------------------------------------
void VulkanMemoryManager::Shutdown()
{
    if (!m_Device) return;

    // Flush any open staging work before freeing
    if (m_StagingOpen)
        SubmitStagingCmd();

    // Free registry-tracked resources (Persistent + FrameBuffered)
    m_Registry.Shutdown();

    // Free staging ring (not registry-tracked - VMM owns it directly)
    VmmRawAlloc::FreeBuffer(*m_Device, m_Ring.buffer);

    // Free transient blocks
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        VmmRawAlloc::FreeBuffer(*m_Device, m_Transient[i].buffer);

    // Destroy the transfer command pool (also implicitly frees any lingering
    // command buffers allocated from it - should be none since SubmitStagingCmd
    // always frees after submit + wait).
    if (m_TransferPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_Device->GetDevice(), m_TransferPool, nullptr);
        m_TransferPool = VK_NULL_HANDLE;
    }

    m_Device  = nullptr;
    m_Command = nullptr;
}

// -----------------------------------------------------------------------------
//  BeginFrame
// -----------------------------------------------------------------------------
void VulkanMemoryManager::BeginFrame(uint32_t frameIndex, uint32_t absoluteFrame)
{
    m_AbsoluteFrame = absoluteFrame;

    // IMPORTANT: the caller must have already waited on the in-flight fence
    // for `frameIndex` before calling BeginFrame.  Otherwise we are about to
    // reset transient storage / free overflow allocs that the GPU may still
    // be reading.

    // Reset the transient block for this frame slot - cursor back to zero,
    // all previous sub-allocations are implicitly discarded.
    m_Transient[frameIndex].Reset();

    // Free any overflow stand-alone TransientFrame allocations from the previous
    // cycle of this slot.
    m_Registry.FreeTransient(frameIndex);

    // Staging ring is drained each EndFrame / FlushStaging (waitIdle model),
    // so no per-slot retire step is needed here.
}

// -----------------------------------------------------------------------------
//  EndFrame
// -----------------------------------------------------------------------------
void VulkanMemoryManager::EndFrame(uint32_t frameIndex)
{
    if (m_StagingOpen)
    {
        // Record the ring's current write head as the tail for this frame slot
        // (diagnostic only under the current waitIdle model - see header).
        m_Ring.frameTails[frameIndex] = m_Ring.writeHead;

        SubmitStagingCmd();

        // SubmitStagingCmd() ends with vkQueueWaitIdle() - every byte the ring
        // handed out this frame has now been consumed.  Drop the cursor back
        // to zero so the ring can be reused from scratch next frame.
        m_Ring.Reset();
    }
}

// -----------------------------------------------------------------------------
//  StageToBuffer
// -----------------------------------------------------------------------------
bool VulkanMemoryManager::StageToBuffer(VmmBuffer&   dst,
                                        const void*  data,
                                        VkDeviceSize size,
                                        VkDeviceSize dstOffset)
{
    if (!m_Ring.HasSpace(size))
    {
        VCKLog::Warn("VMM", std::string("StageToBuffer - staging ring full (")
              + std::to_string(m_Ring.writeHead) + "/"
              + std::to_string(m_Ring.capacity) + " bytes used)");
        return false;
    }

    if (!EnsureStagingCmd()) return false;

    VkDeviceSize srcOffset = m_Ring.Claim(size, 4);
    std::memcpy(static_cast<uint8_t*>(m_Ring.buffer.mapped) + srcOffset,
                data, static_cast<size_t>(size));

    VkBufferCopy region{};
    region.srcOffset = srcOffset;
    region.dstOffset = dstOffset;
    region.size      = size;
    vkCmdCopyBuffer(m_StagingCmd, m_Ring.buffer.buffer, dst.buffer, 1, &region);

    // Release ownership to graphics family when transfer is dedicated.
    // The matching acquire barrier will be recorded on the graphics queue
    // in SubmitStagingCmd after the transfer fence retires.  Spec §7.7.4.
    if (FamiliesDiffer())
    {
        VkBufferMemoryBarrier release{};
        release.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        release.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        release.dstAccessMask       = 0;                           // release half
        release.srcQueueFamilyIndex = m_TransferFamily;
        release.dstQueueFamilyIndex = m_GraphicsFamily;
        release.buffer              = dst.buffer;
        release.offset              = dstOffset;
        release.size                = size;
        vkCmdPipelineBarrier(m_StagingCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 1, &release, 0, nullptr);

        m_PendingAcquireBuffers.push_back({ dst.buffer, size, dstOffset });
    }

    return true;
}

// -----------------------------------------------------------------------------
//  StageToImage
// -----------------------------------------------------------------------------
bool VulkanMemoryManager::StageToImage(VmmImage&    dst,
                                       const void*  data,
                                       VkDeviceSize size,
                                       uint32_t     width,
                                       uint32_t     height)
{
    if (!m_Ring.HasSpace(size))
    {
        VCKLog::Warn("VMM", "StageToImage - staging ring full");
        return false;
    }

    if (!EnsureStagingCmd()) return false;

    VkDeviceSize srcOffset = m_Ring.Claim(size, 4);
    std::memcpy(static_cast<uint8_t*>(m_Ring.buffer.mapped) + srcOffset,
                data, static_cast<size_t>(size));

    // Transition UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image                           = dst.image;
    toTransfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount     = dst.mipLevels;
    toTransfer.subresourceRange.layerCount     = 1;
    toTransfer.srcAccessMask                   = 0;
    toTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(m_StagingCmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    // Copy buffer → image level 0
    VkBufferImageCopy region{};
    region.bufferOffset                    = srcOffset;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent                     = { width, height, 1 };
    vkCmdCopyBufferToImage(m_StagingCmd, m_Ring.buffer.buffer,
                           dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    // Transition TRANSFER_DST → SHADER_READ_ONLY and (when families differ)
    // release ownership to graphics.  The matching acquire barrier will be
    // recorded on the graphics queue in SubmitStagingCmd after the transfer
    // fence retires.  When families alias (same family), this degenerates
    // to a plain layout transition - spec §7.7.4 lets the transition happen
    // in either barrier if both specify matching oldLayout/newLayout.
    VkImageMemoryBarrier toShader = toTransfer;
    toShader.oldLayout             = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout             = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;

    if (FamiliesDiffer())
    {
        // Release half - dstAccessMask is ignored for the release barrier.
        toShader.dstAccessMask         = 0;
        toShader.srcQueueFamilyIndex   = m_TransferFamily;
        toShader.dstQueueFamilyIndex   = m_GraphicsFamily;
        vkCmdPipelineBarrier(m_StagingCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShader);

        m_PendingAcquireImages.push_back({ dst.image, dst.mipLevels });
    }
    else
    {
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(m_StagingCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShader);
    }

    return true;
}

// -----------------------------------------------------------------------------
//  FlushStaging - blocking immediate submit
// -----------------------------------------------------------------------------
void VulkanMemoryManager::FlushStaging()
{
    if (m_StagingOpen)
    {
        SubmitStagingCmd();
        // Ring fully idle after waitIdle - reclaim all space.
        m_Ring.Reset();
    }
}

// -----------------------------------------------------------------------------
//  AllocTransient
// -----------------------------------------------------------------------------
VmmBuffer VulkanMemoryManager::AllocTransient(uint32_t     frameIndex,
                                              const char*  debugName,
                                              VkDeviceSize size,
                                              VkBufferUsageFlags /*usage*/)
{
    TransientBlock& block = m_Transient[frameIndex];

    if (block.HasSpace(size))
    {
        // Sub-allocate from the block - return a view into it
        VkDeviceSize off = block.Claim(size);

        VmmBuffer view;
        view.buffer = block.buffer.buffer;   // shared VkBuffer
        view.alloc  = VK_NULL_HANDLE;        // NOT an owner - do not free
        view.size   = size;
        view.mapped = block.buffer.mapped
                          ? static_cast<uint8_t*>(block.buffer.mapped) + off
                          : nullptr;
        view.id     = 0;   // sub-alloc - not independently registered

        // Sub-allocs are not registered: they have no independent lifetime and
        // tracking each one would swamp the registry.  debugName is intentionally
        // unused in this fast path.
        (void)debugName;

        return view;
    }

    // Overflow: fall back to a standalone allocation
    VCKLog::Warn("VMM", "AllocTransient - block full for slot " + std::to_string(frameIndex) +
          ", falling back to standalone alloc for '" +
          std::string(debugName ? debugName : "?") + "'");

    VmmBuffer buf = VmmRawAlloc::CreateBuffer(
        *m_Device, size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT  |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT   |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        true);

    if (!buf.IsValid()) return buf;

    ResourceInfo info{ Lifetime::TransientFrame, m_AbsoluteFrame, frameIndex, debugName };
    buf.id = m_Registry.Register(buf, info);

    return buf;
}

// -----------------------------------------------------------------------------
//  AllocPersistent (buffer)
// -----------------------------------------------------------------------------
VmmBuffer VulkanMemoryManager::AllocPersistent(const char*        debugName,
                                               VkDeviceSize       size,
                                               VkBufferUsageFlags usage)
{
    VmmBuffer buf = VmmRawAlloc::CreateBuffer(*m_Device, size, usage,
                                              VMA_MEMORY_USAGE_GPU_ONLY);
    if (!buf.IsValid()) return buf;

    ResourceInfo info{ Lifetime::Persistent, m_AbsoluteFrame, 0, debugName };
    m_Registry.Register(buf, info);   // stamps buf.id
    return buf;
}

// -----------------------------------------------------------------------------
//  AllocPersistentImage
// -----------------------------------------------------------------------------
VmmImage VulkanMemoryManager::AllocPersistentImage(const char*        debugName,
                                                   uint32_t           width,
                                                   uint32_t           height,
                                                   VkFormat           format,
                                                   VkImageUsageFlags  usage,
                                                   VkImageAspectFlags aspect,
                                                   uint32_t           mipLevels)
{
    VmmImage img = VmmRawAlloc::CreateImage(*m_Device, width, height,
                                            format, usage, aspect, mipLevels);
    if (!img.IsValid()) return img;

    ResourceInfo info{ Lifetime::Persistent, m_AbsoluteFrame, 0, debugName };
    m_Registry.Register(img, info);   // stamps img.id
    return img;
}

// -----------------------------------------------------------------------------
//  Explicit free
// -----------------------------------------------------------------------------
void VulkanMemoryManager::FreeBuffer(VmmBuffer& buf)
{
    // Remove the registry entry FIRST so FreeAll() at shutdown cannot double
    // free the handle.  Then destroy the GPU resource and clear the handle.
    if (buf.id != 0)
        m_Registry.UnregisterBuffer(buf.id);
    VmmRawAlloc::FreeBuffer(*m_Device, buf);
    buf.id = 0;
}

void VulkanMemoryManager::FreeImage(VmmImage& img)
{
    if (img.id != 0)
        m_Registry.UnregisterImage(img.id);
    VmmRawAlloc::FreeImage(*m_Device, img);
    img.id = 0;
}

// -----------------------------------------------------------------------------
//  LogStats
// -----------------------------------------------------------------------------
void VulkanMemoryManager::LogStats() const
{
    VCKLog::Info("VMM", "Stats - ring: " +
          std::to_string(m_Ring.writeHead) + "/" +
          std::to_string(m_Ring.capacity) + " bytes in use");

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VCKLog::Info("VMM", "  transient[" + std::to_string(i) + "]: " +
              std::to_string(m_Transient[i].offset) + "/" +
              std::to_string(m_Transient[i].capacity) + " bytes used");
    }

    m_Registry.LogStats();
}

// -----------------------------------------------------------------------------
//  Internal staging command helpers
// -----------------------------------------------------------------------------
bool VulkanMemoryManager::EnsureStagingCmd()
{
    if (m_StagingOpen) return true;

    // Allocate a one-shot command buffer from VMM's transfer-family pool.
    // Must match the queue family that SubmitStagingCmd will submit to
    // (VUID-vkQueueSubmit-pCommandBuffers-00074).
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_TransferPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    if (!VK_CHECK(vkAllocateCommandBuffers(m_Device->GetDevice(), &ai, &m_StagingCmd)))
        return false;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (!VK_CHECK(vkBeginCommandBuffer(m_StagingCmd, &bi)))
    {
        vkFreeCommandBuffers(m_Device->GetDevice(),
                             m_TransferPool, 1, &m_StagingCmd);
        m_StagingCmd = VK_NULL_HANDLE;
        return false;
    }

    m_StagingOpen = true;
    return true;
}

void VulkanMemoryManager::SubmitStagingCmd()
{
    if (!m_StagingOpen || !m_StagingCmd) return;

    // If End fails the cmd is malformed and submitting it would be a VUID
    // violation - bail with full cleanup instead of plowing into vkQueueSubmit.
    if (!VK_CHECK(vkEndCommandBuffer(m_StagingCmd)))
    {
        vkFreeCommandBuffers(m_Device->GetDevice(),
                             m_TransferPool, 1, &m_StagingCmd);
        m_StagingCmd  = VK_NULL_HANDLE;
        m_StagingOpen = false;
        // Pending acquires reference release barriers that never actually
        // executed - clear them so the next graphics submit doesn't issue
        // a layout transition against a resource that's still in its
        // pre-staging state.
        m_PendingAcquireBuffers.clear();
        m_PendingAcquireImages.clear();
        return;
    }

    // v0.3: route staging through the (possibly dedicated) transfer queue
    // and use a per-submit VkFence instead of vkQueueWaitIdle.
    //
    // Why: rule 4 only permits vkQueueWaitIdle at Shutdown; using it on the
    // graphics queue during frame work stalls the whole queue, blocking
    // unrelated graphics submits.  A per-submit fence only synchronises
    // this specific upload - the rest of the queue stays hot.  When the
    // physical device exposes a dedicated transfer queue (AMD / NVIDIA
    // usually do; Intel iGPUs often don't) we also peel staging off the
    // graphics queue entirely - the graphics timeline progresses
    // concurrently with the upload.
    VkQueue stagingQueue = m_Device->GetTransferQueue();  // may alias graphics

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_StagingCmd;

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    // Tracks whether the transfer submit actually executed on the GPU so we
    // can decide whether the release barriers ran.  When false, the staging
    // command (containing UNDEFINED->TRANSFER_DST transitions, copies, and
    // release halves) never executed, so the matching acquire halves would
    // issue layout transitions against resources whose actual layout is
    // still UNDEFINED - undefined behavior per spec.
    bool transferSucceeded = false;

    VkFence submitFence = VK_NULL_HANDLE;
    if (!VK_CHECK(vkCreateFence(m_Device->GetDevice(), &fi, nullptr, &submitFence)))
    {
        // Fence creation is cheap and should never fail on a healthy device.
        // Fall back to the legacy path (queue wait) rather than leaking the
        // staging command buffer.  Rule 14: noisy, not silent.
        VCKLog::Warn("VMM", "Staging fence creation failed; falling back to queue wait.");
        if (VK_CHECK(vkQueueSubmit(stagingQueue, 1, &si, VK_NULL_HANDLE)))
        {
            if (VK_CHECK(vkQueueWaitIdle(stagingQueue)))
                transferSucceeded = true;
        }
    }
    else
    {
        if (VK_CHECK(vkQueueSubmit(stagingQueue, 1, &si, submitFence)))
        {
            // Block until this specific upload completes - does NOT stall
            // other work on the same queue, and leaves the graphics queue
            // untouched entirely when a dedicated transfer queue exists.
            vkWaitForFences(m_Device->GetDevice(), 1, &submitFence, VK_TRUE, UINT64_MAX);
            transferSucceeded = true;
        }
        vkDestroyFence(m_Device->GetDevice(), submitFence, nullptr);
    }

    // If the transfer submit failed, the release barriers never executed.
    // Discard pending acquires to avoid issuing layout transitions with a
    // mismatched oldLayout (resource is still in its pre-staging state).
    if (!transferSucceeded)
    {
        m_PendingAcquireBuffers.clear();
        m_PendingAcquireImages.clear();
    }

    vkFreeCommandBuffers(m_Device->GetDevice(),
                         m_TransferPool, 1, &m_StagingCmd);
    m_StagingCmd  = VK_NULL_HANDLE;
    m_StagingOpen = false;

    // ── Acquire half of the queue-family ownership transfer ──────────────────
    //
    // Only needed when the transfer queue family differs from graphics.  At
    // this point the transfer submit has fully retired (we waited on its
    // fence), which per Vulkan spec §7.7.4 satisfies the execution dependency
    // between the release and acquire halves - semaphores would be required
    // only if the two submits overlapped in time.  Record a matching acquire
    // barrier on the graphics queue for every resource StageTo* released.
    if (FamiliesDiffer() &&
        (!m_PendingAcquireBuffers.empty() || !m_PendingAcquireImages.empty()))
    {
        VkCommandBufferAllocateInfo gai{};
        gai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        gai.commandPool        = m_Command->GetCommandPool();         // graphics family
        gai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        gai.commandBufferCount = 1;

        VkCommandBuffer acquireCmd = VK_NULL_HANDLE;
        if (!VK_CHECK(vkAllocateCommandBuffers(m_Device->GetDevice(), &gai, &acquireCmd)))
        {
            VCKLog::Error("VMM", "Failed to allocate acquire-barrier cmd; resources may be in undefined state on graphics queue.");
            m_PendingAcquireBuffers.clear();
            m_PendingAcquireImages.clear();
            return;
        }

        VkCommandBufferBeginInfo gbi{};
        gbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        gbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!VK_CHECK(vkBeginCommandBuffer(acquireCmd, &gbi)))
        {
            // Recording into a not-begun cmd is undefined; bail before we
            // record barriers that would never actually run.  Resources
            // released to graphics family stay stranded - the next staging
            // pass will overwrite layout/access masks anyway, and a future
            // graphics use is the next chance to recover.
            VCKLog::Error("VMM", "Failed to begin acquire-barrier cmd; pending acquires dropped.");
            vkFreeCommandBuffers(m_Device->GetDevice(),
                                 m_Command->GetCommandPool(), 1, &acquireCmd);
            m_PendingAcquireBuffers.clear();
            m_PendingAcquireImages.clear();
            return;
        }

        // Buffer acquires - srcAccessMask ignored; conservative dstAccessMask
        // covers vertex/index/uniform reads (the common post-upload usage).
        std::vector<VkBufferMemoryBarrier> bbs;
        bbs.reserve(m_PendingAcquireBuffers.size());
        for (const auto& p : m_PendingAcquireBuffers)
        {
            VkBufferMemoryBarrier b{};
            b.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            b.srcAccessMask       = 0;
            b.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                                    VK_ACCESS_INDEX_READ_BIT            |
                                    VK_ACCESS_UNIFORM_READ_BIT          |
                                    VK_ACCESS_SHADER_READ_BIT;
            b.srcQueueFamilyIndex = m_TransferFamily;
            b.dstQueueFamilyIndex = m_GraphicsFamily;
            b.buffer              = p.buffer;
            b.offset              = p.offset;
            b.size                = p.size;
            bbs.push_back(b);
        }

        // Image acquires - complete the layout transition (TRANSFER_DST →
        // SHADER_READ_ONLY started by the release half).  Both halves
        // specify matching oldLayout/newLayout, spec §7.7.4 says the
        // transition executes exactly once.
        std::vector<VkImageMemoryBarrier> ibs;
        ibs.reserve(m_PendingAcquireImages.size());
        for (const auto& p : m_PendingAcquireImages)
        {
            VkImageMemoryBarrier b{};
            b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask                   = 0;
            b.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
            b.srcQueueFamilyIndex             = m_TransferFamily;
            b.dstQueueFamilyIndex             = m_GraphicsFamily;
            b.image                           = p.image;
            b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount     = p.mipLevels;
            b.subresourceRange.layerCount     = 1;
            ibs.push_back(b);
        }

        vkCmdPipelineBarrier(acquireCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            static_cast<uint32_t>(bbs.size()), bbs.empty() ? nullptr : bbs.data(),
            static_cast<uint32_t>(ibs.size()), ibs.empty() ? nullptr : ibs.data());

        if (!VK_CHECK(vkEndCommandBuffer(acquireCmd)))
        {
            // End failed - cmd is incomplete, submitting it is a VUID
            // violation.  Free the cmd and drop pending acquires; same
            // reasoning as the BeginCommandBuffer failure path above.
            vkFreeCommandBuffers(m_Device->GetDevice(),
                                 m_Command->GetCommandPool(), 1, &acquireCmd);
            m_PendingAcquireBuffers.clear();
            m_PendingAcquireImages.clear();
            return;
        }

        VkSubmitInfo gsi{};
        gsi.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        gsi.commandBufferCount = 1;
        gsi.pCommandBuffers    = &acquireCmd;

        // The acquire submit MUST execute - skipping it leaves resources in
        // the "released to graphics family" state with no matching acquire,
        // so any subsequent graphics use is UB per spec §7.7.4.  Prefer the
        // fence path; fall back to vkQueueWaitIdle if fence creation or the
        // fenced submit fails so the barriers always actually run.
        VkFence acquireFence = VK_NULL_HANDLE;
        VkFenceCreateInfo afi{};
        afi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        bool acquireFenced = false;
        if (VK_CHECK(vkCreateFence(m_Device->GetDevice(), &afi, nullptr, &acquireFence)))
        {
            if (VK_CHECK(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &gsi, acquireFence)))
            {
                vkWaitForFences(m_Device->GetDevice(), 1, &acquireFence, VK_TRUE, UINT64_MAX);
                acquireFenced = true;
            }
            vkDestroyFence(m_Device->GetDevice(), acquireFence, nullptr);
        }

        if (!acquireFenced)
        {
            VCKLog::Warn("VMM", "Acquire-barrier fence path failed; falling back to queue wait.");
            if (VK_CHECK(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &gsi, VK_NULL_HANDLE)))
                VK_CHECK(vkQueueWaitIdle(m_Device->GetGraphicsQueue()));
        }

        vkFreeCommandBuffers(m_Device->GetDevice(),
                             m_Command->GetCommandPool(), 1, &acquireCmd);

        m_PendingAcquireBuffers.clear();
        m_PendingAcquireImages.clear();
    }
}


} // namespace VCK
