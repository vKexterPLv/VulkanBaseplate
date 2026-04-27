#include "../../VCK.h"
// VCKExpansion.h is pulled in by VCK.h via the aggregate includes.

#include <algorithm>
#include <cstring>
#include <iterator>

namespace VCK {

//  Internal helper
// =============================================================================

static VkFormat FindDepthFormat(VulkanDevice& device)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };
    for (VkFormat fmt : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    return VK_FORMAT_D32_SFLOAT;
}


// =============================================================================
//  [1] VulkanOneTimeCommand
// =============================================================================

bool VulkanOneTimeCommand::Begin(VulkanDevice& device, VulkanCommand& command)
{
    m_Device = &device;
    m_Pool   = command.GetCommandPool();

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_Pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    if (!VK_CHECK(vkAllocateCommandBuffers(device.GetDevice(), &ai, &m_Cmd)))
        return false;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (!VK_CHECK(vkBeginCommandBuffer(m_Cmd, &bi)))
    {
        // Free the cmd we just allocated; if we leave m_Cmd set the caller
        // can't tell Begin failed and a later End() would try to End/Submit
        // a cmd that was never begun (VUID violation) or just leak it.
        vkFreeCommandBuffers(device.GetDevice(), m_Pool, 1, &m_Cmd);
        m_Cmd = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanOneTimeCommand::End()
{
    if (!m_Cmd) return;

    VK_CHECK(vkEndCommandBuffer(m_Cmd));

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_Cmd;

    // v0.3: per-submit fence instead of vkQueueWaitIdle.  Same reasoning
    // as VulkanMemoryManager::SubmitStagingCmd - rule 4 shrinks, only the
    // specific one-shot submit is waited on, the rest of the graphics
    // queue stays hot.
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence submitFence = VK_NULL_HANDLE;
    if (!VK_CHECK(vkCreateFence(m_Device->GetDevice(), &fi, nullptr, &submitFence)))
    {
        // Rule 14: loud fallback.
        VCKLog::Warn("OneTime", "Fence creation failed; falling back to queue wait.");
        VK_CHECK(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(m_Device->GetGraphicsQueue()));
    }
    else
    {
        // Only wait on the fence if the submit itself succeeded - otherwise
        // the fence is never signaled and vkWaitForFences(UINT64_MAX) would
        // hang forever.  VK_CHECK logs the VkResult on failure (rule 14).
        if (VK_CHECK(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &si, submitFence)))
        {
            vkWaitForFences(m_Device->GetDevice(), 1, &submitFence, VK_TRUE, UINT64_MAX);
        }
        vkDestroyFence(m_Device->GetDevice(), submitFence, nullptr);
    }

    vkFreeCommandBuffers(m_Device->GetDevice(), m_Pool, 1, &m_Cmd);
    m_Cmd = VK_NULL_HANDLE;
}


// =============================================================================
//  [2] VulkanFramebufferSet
// =============================================================================

bool VulkanFramebufferSet::Initialize(VulkanDevice& device, VulkanSwapchain& swapchain,
                                       VulkanPipeline& pipeline)
{
    m_Device    = &device;
    m_Swapchain = &swapchain;
    return CreateAll(pipeline.GetRenderPass(), VK_NULL_HANDLE);
}

bool VulkanFramebufferSet::Initialize(VulkanDevice& device, VulkanSwapchain& swapchain,
                                       VulkanPipeline& pipeline, VulkanDepthBuffer& depth)
{
    m_Device    = &device;
    m_Swapchain = &swapchain;
    return CreateAll(pipeline.GetRenderPass(), depth.GetImageView());
}

bool VulkanFramebufferSet::Initialize(VulkanDevice& device, VulkanSwapchain& swapchain,
                                       VkRenderPass renderPass, VkImageView depthView)
{
    m_Device    = &device;
    m_Swapchain = &swapchain;
    return CreateAll(renderPass, depthView);
}

void VulkanFramebufferSet::Shutdown()
{
    DestroyAll();
    m_Device    = nullptr;
    m_Swapchain = nullptr;
}

bool VulkanFramebufferSet::Recreate(VulkanPipeline& pipeline)
{
    DestroyAll();
    return CreateAll(pipeline.GetRenderPass(), VK_NULL_HANDLE);
}

bool VulkanFramebufferSet::Recreate(VulkanPipeline& pipeline, VulkanDepthBuffer& depth)
{
    DestroyAll();
    return CreateAll(pipeline.GetRenderPass(), depth.GetImageView());
}

bool VulkanFramebufferSet::Recreate(VkRenderPass renderPass, VkImageView depthView)
{
    DestroyAll();
    return CreateAll(renderPass, depthView);
}

VkFramebuffer VulkanFramebufferSet::Get(uint32_t imageIndex) const
{
    if (imageIndex >= static_cast<uint32_t>(m_Framebuffers.size()))
        return VK_NULL_HANDLE;
    return m_Framebuffers[imageIndex];
}

bool VulkanFramebufferSet::CreateAll(VkRenderPass renderPass, VkImageView depthView)
{
    const auto& views  = m_Swapchain->GetImageViews();
    VkExtent2D  extent = m_Swapchain->GetExtent();

    // When MSAA is enabled the pipeline's render pass declares
    // [msaaColor, resolveColor] in that order, so the framebuffer has to bind
    // the per-image MSAA view at slot 0 and the single-sample swapchain view
    // at slot 1 (as the resolve target).  Without MSAA, slot 0 is just the
    // swapchain view.  Depth view (when provided) is always appended last so
    // it matches the render pass attachment order.
    const bool              useMsaa = m_Swapchain->HasMSAA();
    std::vector<VkImageView> msaaViews = useMsaa
        ? m_Swapchain->GetMSAAColorViews()
        : std::vector<VkImageView>{};

    m_Framebuffers.resize(views.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < views.size(); i++)
    {
        std::vector<VkImageView> attachments;
        if (useMsaa)
        {
            attachments.push_back(msaaViews[i]);   // colour   (multisampled)
            attachments.push_back(views[i]);       // resolve  (swapchain)
        }
        else
        {
            attachments.push_back(views[i]);       // colour   (swapchain)
        }
        if (depthView != VK_NULL_HANDLE)
            attachments.push_back(depthView);

        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = renderPass;
        fi.attachmentCount = static_cast<uint32_t>(attachments.size());
        fi.pAttachments    = attachments.data();
        fi.width           = extent.width;
        fi.height          = extent.height;
        fi.layers          = 1;

        if (!VK_CHECK(vkCreateFramebuffer(m_Device->GetDevice(), &fi, nullptr, &m_Framebuffers[i])))
            return false;
    }
    return true;
}

void VulkanFramebufferSet::DestroyAll()
{
    if (!m_Device) return;
    for (VkFramebuffer fb : m_Framebuffers)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(m_Device->GetDevice(), fb, nullptr);
    m_Framebuffers.clear();
}


// =============================================================================
//  HandleLiveResize  -  free function; lives here so the swapchain /
//  framebuffer / pipeline / (optional) depth references are all in scope.
//
//  Uses VCKLog::Notice so the resize trigger / completion lines always show
//  up (rule 6 + 12 - explicit, observable recreation events) without
//  requiring cfg.debug.  The DebugTimeline overloads additionally emit a
//  "HandleLiveResize" CPU span so a recorded timeline attributes the pause.
// =============================================================================
bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline)
{
    if (!window.WasResized())  return false;
    if (window.IsMinimized())  return false;

    const uint32_t w = static_cast<uint32_t>(window.GetWidth());
    const uint32_t h = static_cast<uint32_t>(window.GetHeight());

    VCKLog::Notice("LiveResize",
        "triggered - " + std::to_string(w) + "x" + std::to_string(h));
    vkDeviceWaitIdle(device.GetDevice());
    if (!swapchain.Recreate(w, h))          { window.ClearResized(); return false; }
    if (!framebuffers.Recreate(pipeline))   { window.ClearResized(); return false; }
    window.ClearResized();
    VCKLog::Notice("LiveResize", "done");
    return true;
}

bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      VulkanDepthBuffer&    depth)
{
    if (!window.WasResized())  return false;
    if (window.IsMinimized())  return false;

    const uint32_t w = static_cast<uint32_t>(window.GetWidth());
    const uint32_t h = static_cast<uint32_t>(window.GetHeight());

    VCKLog::Notice("LiveResize",
        "triggered - " + std::to_string(w) + "x" + std::to_string(h) + " (with depth)");
    vkDeviceWaitIdle(device.GetDevice());
    if (!swapchain.Recreate(w, h))                 { window.ClearResized(); return false; }
    if (!depth.Recreate(w, h))                     { window.ClearResized(); return false; }
    if (!framebuffers.Recreate(pipeline, depth))   { window.ClearResized(); return false; }
    window.ClearResized();
    VCKLog::Notice("LiveResize", "done (with depth)");
    return true;
}

// ---- v0.3: scheduler-aware overloads ---------------------------------------
//
// When a FrameScheduler owns the frame loop, use its DrainInFlight() to
// wait only on the scheduler's submitted work instead of vkDeviceWaitIdle.
// Independent work on dedicated compute / transfer queues (VMM uploads,
// AsyncComputeExample dispatches) keeps progressing during the resize.
// Rule 4: this replaces the last runtime vkDeviceWaitIdle on the hot path
// - Shutdown still uses it (allow-list entry).
bool HandleLiveResize(Window&               window,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      FrameScheduler&       scheduler)
{
    if (!window.WasResized())  return false;
    if (window.IsMinimized())  return false;

    const uint32_t w = static_cast<uint32_t>(window.GetWidth());
    const uint32_t h = static_cast<uint32_t>(window.GetHeight());

    VCKLog::Notice("LiveResize",
        "triggered - " + std::to_string(w) + "x" + std::to_string(h) + " (scheduler drain)");

    if (scheduler.Timeline().Enabled())
        scheduler.Timeline().BeginCpuSpan("HandleLiveResize", scheduler.AbsoluteFrame());

    scheduler.DrainInFlight();
    const bool ok_sc  = swapchain.Recreate(w, h, /*drainedExternally=*/true);
    const bool ok_fb  = ok_sc && framebuffers.Recreate(pipeline);

    if (scheduler.Timeline().Enabled())
        scheduler.Timeline().EndCpuSpan("HandleLiveResize", scheduler.AbsoluteFrame());

    window.ClearResized();
    if (!ok_sc || !ok_fb) return false;
    VCKLog::Notice("LiveResize", "done (scheduler drain)");
    return true;
}

bool HandleLiveResize(Window&               window,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      VulkanDepthBuffer&    depth,
                      FrameScheduler&       scheduler)
{
    if (!window.WasResized())  return false;
    if (window.IsMinimized())  return false;

    const uint32_t w = static_cast<uint32_t>(window.GetWidth());
    const uint32_t h = static_cast<uint32_t>(window.GetHeight());

    VCKLog::Notice("LiveResize",
        "triggered - " + std::to_string(w) + "x" + std::to_string(h) + " (scheduler drain, with depth)");

    if (scheduler.Timeline().Enabled())
        scheduler.Timeline().BeginCpuSpan("HandleLiveResize", scheduler.AbsoluteFrame());

    scheduler.DrainInFlight();
    const bool ok_sc  = swapchain.Recreate(w, h, /*drainedExternally=*/true);
    const bool ok_dp  = ok_sc && depth.Recreate(w, h);
    const bool ok_fb  = ok_dp && framebuffers.Recreate(pipeline, depth);

    if (scheduler.Timeline().Enabled())
        scheduler.Timeline().EndCpuSpan("HandleLiveResize", scheduler.AbsoluteFrame());

    window.ClearResized();
    if (!ok_sc || !ok_dp || !ok_fb) return false;
    VCKLog::Notice("LiveResize", "done (scheduler drain, with depth)");
    return true;
}


// =============================================================================
//  [3] VulkanDepthBuffer
// =============================================================================

bool VulkanDepthBuffer::Initialize(VulkanDevice& device, uint32_t width, uint32_t height)
{
    m_Device = &device;
    VkFormat fmt = FindDepthFormat(device);
    return m_Image.Create(device, width, height, fmt,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanDepthBuffer::Shutdown()
{
    m_Image.Shutdown();
    m_Device = nullptr;
}

bool VulkanDepthBuffer::Recreate(uint32_t width, uint32_t height)
{
    m_Image.Shutdown();
    VkFormat fmt = FindDepthFormat(*m_Device);
    return m_Image.Create(*m_Device, width, height, fmt,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);
}


// =============================================================================
//  [4] VulkanSampler
// =============================================================================

bool VulkanSampler::CreateNearest(VulkanDevice& device)
{
    return Create(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST);
}

bool VulkanSampler::CreateLinear(VulkanDevice& device)
{
    return Create(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);
}

bool VulkanSampler::Create(VulkanDevice& device, VkFilter filter, VkSamplerMipmapMode mipMode)
{
    m_Device = &device;

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = filter;
    si.minFilter    = filter;
    si.mipmapMode   = mipMode;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod       = VK_LOD_CLAMP_NONE;

    return VK_CHECK(vkCreateSampler(device.GetDevice(), &si, nullptr, &m_Sampler));
}

void VulkanSampler::Shutdown()
{
    if (m_Sampler && m_Device)
        vkDestroySampler(m_Device->GetDevice(), m_Sampler, nullptr);
    m_Sampler = VK_NULL_HANDLE;
    m_Device  = nullptr;
}


// =============================================================================
//  [5] VulkanTexture
// =============================================================================

bool VulkanTexture::CreateFromPixels(VulkanDevice& device, VulkanCommand& command,
                                      const uint8_t* pixels, uint32_t width, uint32_t height)
{
    VkDeviceSize size = (VkDeviceSize)width * height * 4;

    if (!m_Image.Create(device, width, height,
                        VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT))
        return false;

    // Upload via staging
    VulkanBuffer staging;
    if (!staging.CreateStaging(device, size))
        return false;

    if (!staging.Upload(pixels, size))
    {
        staging.Shutdown();
        return false;
    }

    VulkanOneTimeCommand otc;
    if (!otc.Begin(device, command))
    {
        staging.Shutdown();
        return false;
    }

    m_Image.RecordLayoutTransition(otc.Cmd(),
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    m_Image.RecordCopyFromBuffer(otc.Cmd(), staging, width, height);
    m_Image.RecordLayoutTransition(otc.Cmd(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    otc.End();
    staging.Shutdown();

    return m_Sampler.CreateLinear(device);
}

void VulkanTexture::Shutdown()
{
    m_Sampler.Shutdown();
    m_Image.Shutdown();
}


// =============================================================================
//  [6] VulkanMesh
// =============================================================================

bool VulkanMesh::Upload(VulkanDevice& device, VulkanCommand& command,
                         const void* vertices, VkDeviceSize vertexSize,
                         const uint32_t* indices, uint32_t indexCount)
{
    // Back-compat entry point - treats the mesh as purely indexed.
    return Upload(device, command, vertices, vertexSize, 0u, indices, indexCount);
}

bool VulkanMesh::Upload(VulkanDevice& device, VulkanCommand& command,
                         const void* vertices, VkDeviceSize vertexSize,
                         uint32_t vertexCount,
                         const uint32_t* indices, uint32_t indexCount)
{
    m_VertexCount = vertexCount;
    m_IndexCount  = indexCount;

    // ── Vertex buffer ────────────────────────────────────────────────────────
    {
        VulkanBuffer staging;
        if (!staging.CreateStaging(device, vertexSize))
            return false;

        if (!staging.Upload(vertices, vertexSize))
        {
            staging.Shutdown();
            return false;
        }

        if (!m_VertexBuffer.CreateVertex(device, vertexSize))
        {
            staging.Shutdown();
            return false;
        }

        VulkanOneTimeCommand otc;
        if (!otc.Begin(device, command))
        {
            staging.Shutdown();
            return false;
        }
        staging.RecordCopyTo(otc.Cmd(), m_VertexBuffer, vertexSize);
        otc.End();

        staging.Shutdown();   // ← explicit free - VulkanBuffer has no destructor
    }

    // ── Index buffer ─────────────────────────────────────────────────────────
    if (indexCount > 0 && indices)
    {
        VkDeviceSize idxSize = sizeof(uint32_t) * indexCount;

        VulkanBuffer staging;
        if (!staging.CreateStaging(device, idxSize))
            return false;

        if (!staging.Upload(indices, idxSize))
        {
            staging.Shutdown();
            return false;
        }

        if (!m_IndexBuffer.CreateIndex(device, idxSize))
        {
            staging.Shutdown();
            return false;
        }

        VulkanOneTimeCommand otc;
        if (!otc.Begin(device, command))
        {
            staging.Shutdown();
            return false;
        }
        staging.RecordCopyTo(otc.Cmd(), m_IndexBuffer, idxSize);
        otc.End();

        staging.Shutdown();   // ← explicit free
    }

    return true;
}

void VulkanMesh::Shutdown()
{
    m_IndexBuffer.Shutdown();
    m_VertexBuffer.Shutdown();
    m_VertexCount = 0;
    m_IndexCount  = 0;
}

void VulkanMesh::RecordDraw(VkCommandBuffer cmd) const
{
    VkBuffer     buf    = m_VertexBuffer.GetBuffer();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);

    if (m_IndexBuffer.IsValid())
    {
        vkCmdBindIndexBuffer(cmd, m_IndexBuffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_IndexCount, 1, 0, 0, 0);
    }
    else
    {
        // Non-indexed: use explicit vertex count if supplied; fall back to
        // m_IndexCount for legacy callers that used the older Upload overload
        // with a non-null index array.
        const uint32_t count = m_VertexCount != 0 ? m_VertexCount : m_IndexCount;
        vkCmdDraw(cmd, count, 1, 0, 0);
    }
}


// =============================================================================
//  [7] VulkanDescriptorLayoutBuilder
// =============================================================================

VulkanDescriptorLayoutBuilder& VulkanDescriptorLayoutBuilder::Add(
    uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages)
{
    VkDescriptorSetLayoutBinding b{};
    b.binding         = binding;
    b.descriptorType  = type;
    b.descriptorCount = 1;
    b.stageFlags      = stages;
    m_Bindings.push_back(b);
    return *this;
}

VkDescriptorSetLayout VulkanDescriptorLayoutBuilder::Build(VulkanDevice& device) const
{
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(m_Bindings.size());
    ci.pBindings    = m_Bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device.GetDevice(), &ci, nullptr, &layout));
    return layout;
}


// =============================================================================
//  [8] VulkanDescriptorPool
// =============================================================================

bool VulkanDescriptorPool::Initialize(VulkanDevice& device,
                                       VkDescriptorSetLayout layout,
                                       VkDescriptorType type)
{
    m_Device = &device;

    VkDescriptorPoolSize poolSize{};
    poolSize.type            = type;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = MAX_FRAMES_IN_FLIGHT;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &poolSize;

    if (!VK_CHECK(vkCreateDescriptorPool(device.GetDevice(), &ci, nullptr, &m_Pool)))
        return false;

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(layout);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_Pool;
    ai.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    ai.pSetLayouts        = layouts.data();

    return VK_CHECK(vkAllocateDescriptorSets(device.GetDevice(), &ai, m_Sets.data()));
}

void VulkanDescriptorPool::Shutdown()
{
    if (m_Pool && m_Device)
        vkDestroyDescriptorPool(m_Device->GetDevice(), m_Pool, nullptr);
    m_Pool   = VK_NULL_HANDLE;
    m_Device = nullptr;
}


// =============================================================================
//  [10] VulkanDescriptorAllocator
// =============================================================================

bool VulkanDescriptorAllocator::Initialize(
    VulkanDevice& device,
    uint32_t      maxSets,
    std::initializer_list<PoolSize> sizes)
{
    m_Device = &device;

    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(sizes.size());
    for (const auto& s : sizes)
    {
        VkDescriptorPoolSize ps{};
        ps.type            = s.type;
        ps.descriptorCount = s.count;
        poolSizes.push_back(ps);
    }

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = maxSets;
    ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    ci.pPoolSizes    = poolSizes.data();

    return VK_CHECK(vkCreateDescriptorPool(device.GetDevice(), &ci, nullptr, &m_Pool));
}

void VulkanDescriptorAllocator::Shutdown()
{
    if (m_Pool && m_Device)
        vkDestroyDescriptorPool(m_Device->GetDevice(), m_Pool, nullptr);
    m_Pool   = VK_NULL_HANDLE;
    m_Device = nullptr;
}

VkDescriptorSet VulkanDescriptorAllocator::Allocate(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_Pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!VK_CHECK(vkAllocateDescriptorSets(m_Device->GetDevice(), &ai, &set)))
        return VK_NULL_HANDLE;
    return set;
}


// =============================================================================
//  [11] VulkanModelPipeline
// =============================================================================

bool VulkanModelPipeline::Initialize(VulkanDevice&                          device,
                                     VkRenderPass                           renderPass,
                                     const VulkanPipeline::ShaderInfo&      shaders,
                                     const VulkanPipeline::VertexInputInfo& vertexInput)
{
    // 4-arg convenience overload: assumes 1x MSAA.  Emit a loud warning so
    // users do not silently ship a pipeline whose sample count does not match
    // an MSAA render pass (Vulkan spec: rasterisation is undefined when
    // pipeline.multisample.rasterizationSamples != subpass.samples; on NVIDIA
    // this commonly appears as rendering confined to the top-left quadrant).
    VCKLog::Warn("VulkanModelPipeline",
        "Initialize(4 args) assumes VK_SAMPLE_COUNT_1_BIT; if the render pass "
        "uses MSAA (see swapchain.GetMSAASamples()) pass it explicitly via the "
        "5-arg overload to avoid sample-count mismatch.");
    return Initialize(device, renderPass, shaders, vertexInput,
                      VK_SAMPLE_COUNT_1_BIT);
}

bool VulkanModelPipeline::Initialize(VulkanDevice&                          device,
                                     VkRenderPass                           renderPass,
                                     const VulkanPipeline::ShaderInfo&      shaders,
                                     const VulkanPipeline::VertexInputInfo& vertexInput,
                                     VkSampleCountFlagBits                  samples)
{
    m_Device = &device;

    if (!BuildDescriptorLayouts())                                           return false;
    if (!BuildPipelineLayout())                                              return false;
    if (!BuildGraphicsPipeline(renderPass, shaders, vertexInput, samples))   return false;

    VCKLog::Info("ModelPipeline", "Initialized");
    return true;
}

void VulkanModelPipeline::Shutdown()
{
    if (!m_Device) return;
    VkDevice dev = m_Device->GetDevice();

    if (m_Pipeline)       { vkDestroyPipeline(dev,             m_Pipeline,       nullptr); m_Pipeline       = VK_NULL_HANDLE; }
    if (m_PipelineLayout) { vkDestroyPipelineLayout(dev,       m_PipelineLayout, nullptr); m_PipelineLayout = VK_NULL_HANDLE; }
    if (m_Set1Layout)     { vkDestroyDescriptorSetLayout(dev,  m_Set1Layout,     nullptr); m_Set1Layout     = VK_NULL_HANDLE; }
    if (m_Set0Layout)     { vkDestroyDescriptorSetLayout(dev,  m_Set0Layout,     nullptr); m_Set0Layout     = VK_NULL_HANDLE; }

    m_Device = nullptr;
    VCKLog::Info("ModelPipeline", "Shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Descriptor layouts
//    set 0: binding 0 = per-frame UBO (vert)
//    set 1: binding 0 = combined image sampler (frag)
// ─────────────────────────────────────────────────────────────────────────────
bool VulkanModelPipeline::BuildDescriptorLayouts()
{
    m_Set0Layout = VulkanDescriptorLayoutBuilder{}
        .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT)
        .Build(*m_Device);

    if (m_Set0Layout == VK_NULL_HANDLE) return false;

    m_Set1Layout = VulkanDescriptorLayoutBuilder{}
        .Add(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT)
        .Build(*m_Device);

    return m_Set1Layout != VK_NULL_HANDLE;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pipeline layout
//    set 0, set 1 + push constant (64 bytes, vertex stage - mat4 model)
// ─────────────────────────────────────────────────────────────────────────────
bool VulkanModelPipeline::BuildPipelineLayout()
{
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset     = 0;
    pcRange.size       = 64; // mat4 model

    std::array<VkDescriptorSetLayout, 2> setLayouts = { m_Set0Layout, m_Set1Layout };

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = static_cast<uint32_t>(setLayouts.size());
    ci.pSetLayouts            = setLayouts.data();
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &pcRange;

    return VK_CHECK(vkCreatePipelineLayout(m_Device->GetDevice(), &ci, nullptr, &m_PipelineLayout));
}

// ─────────────────────────────────────────────────────────────────────────────
VkShaderModule VulkanModelPipeline::CreateShaderModule(const std::vector<uint32_t>& spirv)
{
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode    = spirv.data();

    VkShaderModule mod = VK_NULL_HANDLE;
    if (!VK_CHECK(vkCreateShaderModule(m_Device->GetDevice(), &ci, nullptr, &mod)))
        return VK_NULL_HANDLE;
    return mod;
}

// ─────────────────────────────────────────────────────────────────────────────
//  BuildGraphicsPipeline
//  Same fixed state as VulkanPipeline, but uses m_PipelineLayout so the
//  shader's descriptor set and push constant bindings are satisfied.
// ─────────────────────────────────────────────────────────────────────────────
bool VulkanModelPipeline::BuildGraphicsPipeline(
    VkRenderPass                              renderPass,
    const VulkanPipeline::ShaderInfo&         shaders,
    const VulkanPipeline::VertexInputInfo&    vertexInput,
    VkSampleCountFlagBits                     samples)
{
    VkShaderModule vertMod = CreateShaderModule(shaders.VertexSpirv);
    VkShaderModule fragMod = CreateShaderModule(shaders.FragmentSpirv);

    if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
    {
        if (vertMod) vkDestroyShaderModule(m_Device->GetDevice(), vertMod, nullptr);
        if (fragMod) vkDestroyShaderModule(m_Device->GetDevice(), fragMod, nullptr);
        return false;
    }

    // ── Shader stages ─────────────────────────────────────────────────────────
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";

    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    // ── Vertex input ──────────────────────────────────────────────────────────
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = static_cast<uint32_t>(vertexInput.Bindings.size());
    vi.pVertexBindingDescriptions      = vertexInput.Bindings.empty()   ? nullptr : vertexInput.Bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInput.Attributes.size());
    vi.pVertexAttributeDescriptions    = vertexInput.Attributes.empty() ? nullptr : vertexInput.Attributes.data();

    // ── Input assembly ────────────────────────────────────────────────────────
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia.primitiveRestartEnable = VK_FALSE;

    // ── Dynamic state (viewport + scissor) ───────────────────────────────────
    std::array<VkDynamicState, 2> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dynState.pDynamicStates    = dyn.data();

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount  = 1;

    // ── Rasterizer ────────────────────────────────────────────────────────────
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode             = VK_POLYGON_MODE_FILL;
    raster.lineWidth               = 1.0f;
    raster.cullMode                = VK_CULL_MODE_BACK_BIT;
    raster.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthClampEnable        = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.depthBiasEnable         = VK_FALSE;

    // ── Multisampling ─────────────────────────────────────────────────────────
    // Must match the render pass colour attachment samples.  Users of
    // VulkanModelPipeline with MSAA should pass swapchain.GetMSAASamples() to
    // Initialize(); the default overload uses 1x.
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;
    ms.sampleShadingEnable  = VK_FALSE;

    // ── Colour blend (alpha blend) ────────────────────────────────────────────
    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable         = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.logicOpEnable   = VK_FALSE;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttach;

    // ── Assemble ──────────────────────────────────────────────────────────────
    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = static_cast<uint32_t>(stages.size());
    pci.pStages             = stages.data();
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vps;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = nullptr;  // no depth attachment yet
    pci.pColorBlendState    = &blend;
    pci.pDynamicState       = &dynState;
    pci.layout              = m_PipelineLayout;  // ← full layout with descriptors + push constants
    pci.renderPass          = renderPass;
    pci.subpass             = 0;
    pci.basePipelineHandle  = VK_NULL_HANDLE;

    bool ok = VK_CHECK(vkCreateGraphicsPipelines(
        m_Device->GetDevice(), VK_NULL_HANDLE, 1, &pci, nullptr, &m_Pipeline));

    vkDestroyShaderModule(m_Device->GetDevice(), vertMod, nullptr);
    vkDestroyShaderModule(m_Device->GetDevice(), fragMod, nullptr);

    return ok;
}


// =============================================================================
//  [12] VulkanMipmapGenerator
// =============================================================================

uint32_t VulkanMipmapGenerator::MipLevels(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    uint32_t dim    = (width > height) ? width : height;
    while (dim > 1) { dim >>= 1; ++levels; }
    return levels;
}

bool VulkanMipmapGenerator::IsFormatSupported(VulkanDevice& device, VkFormat format)
{
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), format, &props);
    return (props.optimalTilingFeatures &
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

bool VulkanMipmapGenerator::Generate(VulkanDevice&  device,
                                     VulkanCommand& command,
                                     VkImage        image,
                                     uint32_t       width,
                                     uint32_t       height,
                                     uint32_t       mipLevels)
{
    if (mipLevels <= 1) return true;   // nothing to generate

    VulkanOneTimeCommand otc;
    if (!otc.Begin(device, command))
        return false;

    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image                           = image;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    barrier.subresourceRange.levelCount     = 1;

    int32_t mipW = static_cast<int32_t>(width);
    int32_t mipH = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mipLevels; i++)
    {
        // Transition level i-1 from TRANSFER_DST → TRANSFER_SRC so it can be read
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(otc.Cmd(),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Blit level i-1 → level i (half dimensions, clamped to 1)
        int32_t nextW = (mipW > 1) ? mipW / 2 : 1;
        int32_t nextH = (mipH > 1) ? mipH / 2 : 1;

        VkImageBlit blit{};
        blit.srcOffsets[0]               = { 0, 0, 0 };
        blit.srcOffsets[1]               = { mipW, mipH, 1 };
        blit.srcSubresource.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel     = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount   = 1;
        blit.dstOffsets[0]               = { 0, 0, 0 };
        blit.dstOffsets[1]               = { nextW, nextH, 1 };
        blit.dstSubresource.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel     = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount   = 1;

        vkCmdBlitImage(otc.Cmd(),
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // Transition level i-1 from TRANSFER_SRC → SHADER_READ_ONLY - done with it
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(otc.Cmd(),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        mipW = nextW;
        mipH = nextH;
    }

    // Transition the last mip level (still in TRANSFER_DST) → SHADER_READ_ONLY
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(otc.Cmd(),
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    otc.End();
    return true;
}


// =============================================================================
// [23] VertexLayout
// =============================================================================
namespace
{
    struct VertexAttrInfo
    {
        uint32_t size;
        VkFormat format;
    };

    VertexAttrInfo VertexAttrMeta(VertexAttrType t)
    {
        switch (t)
        {
            case VertexAttrType::Float: return { 4,  VK_FORMAT_R32_SFLOAT };
            case VertexAttrType::Vec2:  return { 8,  VK_FORMAT_R32G32_SFLOAT };
            case VertexAttrType::Vec3:  return { 12, VK_FORMAT_R32G32B32_SFLOAT };
            case VertexAttrType::Vec4:  return { 16, VK_FORMAT_R32G32B32A32_SFLOAT };
            case VertexAttrType::Int:   return { 4,  VK_FORMAT_R32_SINT };
            case VertexAttrType::UInt:  return { 4,  VK_FORMAT_R32_UINT };
        }
        return { 0, VK_FORMAT_UNDEFINED };
    }
}

VertexLayout& VertexLayout::Add(const char* name, VertexAttrType t)
{
    const VertexAttrInfo info = VertexAttrMeta(t);
    if (info.size == 0)
    {
        VCKLog::Error("VertexLayout",
            std::string("unknown attribute type passed to Add(\"") +
            (name ? name : "?") + "\")");
        return *this;
    }

    Entry e{};
    e.name   = name;
    e.type   = t;
    e.offset = m_Stride;
    e.size   = info.size;
    e.format = info.format;
    m_Attrs.push_back(e);
    m_Stride += info.size;
    return *this;
}

VkVertexInputBindingDescription VertexLayout::Binding(uint32_t binding) const
{
    VkVertexInputBindingDescription b{};
    b.binding   = binding;
    b.stride    = m_Stride;
    b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return b;
}

std::vector<VkVertexInputAttributeDescription> VertexLayout::Attributes(uint32_t binding) const
{
    std::vector<VkVertexInputAttributeDescription> out;
    out.reserve(m_Attrs.size());
    for (std::size_t i = 0; i < m_Attrs.size(); ++i)
    {
        VkVertexInputAttributeDescription a{};
        a.binding  = binding;
        a.location = static_cast<uint32_t>(i);
        a.format   = m_Attrs[i].format;
        a.offset   = m_Attrs[i].offset;
        out.push_back(a);
    }
    return out;
}


// =============================================================================
// [24] PushConstants
// =============================================================================
namespace
{
    uint32_t PushConstTypeSize(PushConstType t)
    {
        switch (t)
        {
            case PushConstType::Float: return 4;
            case PushConstType::Vec2:  return 8;
            case PushConstType::Vec3:  return 12;
            case PushConstType::Vec4:  return 16;
            case PushConstType::Mat4:  return 64;
            case PushConstType::Int:   return 4;
            case PushConstType::UInt:  return 4;
        }
        return 0;
    }

    const char* PushConstTypeName(PushConstType t)
    {
        switch (t)
        {
            case PushConstType::Float: return "Float";
            case PushConstType::Vec2:  return "Vec2";
            case PushConstType::Vec3:  return "Vec3";
            case PushConstType::Vec4:  return "Vec4";
            case PushConstType::Mat4:  return "Mat4";
            case PushConstType::Int:   return "Int";
            case PushConstType::UInt:  return "UInt";
        }
        return "?";
    }

    bool SameCStr(const char* a, const char* b)
    {
        if (a == b) return true;
        if (a == nullptr || b == nullptr) return false;
        while (*a && *a == *b) { ++a; ++b; }
        return *a == *b;
    }
}

PushConstants& PushConstants::Declare(const char* name, PushConstType t)
{
    const uint32_t size = PushConstTypeSize(t);
    if (size == 0 || name == nullptr)
    {
        VCKLog::Error("PushConstants",
            std::string("Declare got unknown type or null name"));
        return *this;
    }

    // Duplicate name guard.
    for (const Slot& s : m_Slots)
    {
        if (SameCStr(s.name, name))
        {
            VCKLog::Error("PushConstants",
                std::string("Declare(\"") + name + "\") - name already declared");
            return *this;
        }
    }

    Slot s{};
    s.name   = name;
    s.type   = t;
    s.offset = static_cast<uint32_t>(m_Buffer.size());
    s.size   = size;
    m_Slots.push_back(s);
    m_Buffer.resize(m_Buffer.size() + size, 0);
    return *this;
}

uint8_t* PushConstants::SlotWrite(const char* name, PushConstType expected, uint32_t expectedSize)
{
    for (const Slot& s : m_Slots)
    {
        if (!SameCStr(s.name, name)) continue;
        if (s.type != expected)
        {
            VCKLog::Error("PushConstants",
                std::string("Set(\"") + name + "\") type mismatch: slot is " +
                PushConstTypeName(s.type) + ", caller passed " +
                PushConstTypeName(expected));
            return nullptr;
        }
        if (s.size != expectedSize)
        {
            VCKLog::Error("PushConstants",
                std::string("Set(\"") + name + "\") size mismatch");
            return nullptr;
        }
        return m_Buffer.data() + s.offset;
    }
    VCKLog::Error("PushConstants",
        std::string("Set(\"") + (name ? name : "?") + "\") - name not declared");
    return nullptr;
}

PushConstants& PushConstants::Set(const char* name, float v)
{
    if (uint8_t* p = SlotWrite(name, PushConstType::Float, 4))
        std::memcpy(p, &v, 4);
    return *this;
}
PushConstants& PushConstants::Set(const char* name, const Vec2& v)
{
    if (uint8_t* p = SlotWrite(name, PushConstType::Vec2, 8))
        std::memcpy(p, &v, 8);
    return *this;
}
PushConstants& PushConstants::Set(const char* name, const Vec3& v)
{
    if (uint8_t* p = SlotWrite(name, PushConstType::Vec3, 12))
        std::memcpy(p, &v, 12);
    return *this;
}
PushConstants& PushConstants::Set(const char* name, const Vec4& v)
{
    if (uint8_t* p = SlotWrite(name, PushConstType::Vec4, 16))
        std::memcpy(p, &v, 16);
    return *this;
}
PushConstants& PushConstants::Set(const char* name, const Mat4& v)
{
    if (uint8_t* p = SlotWrite(name, PushConstType::Mat4, 64))
        std::memcpy(p, &v, 64);
    return *this;
}
PushConstants& PushConstants::Set(const char* name, int32_t v)
{
    if (uint8_t* p = SlotWrite(name, PushConstType::Int, 4))
        std::memcpy(p, &v, 4);
    return *this;
}
PushConstants& PushConstants::Set(const char* name, uint32_t v)
{
    if (uint8_t* p = SlotWrite(name, PushConstType::UInt, 4))
        std::memcpy(p, &v, 4);
    return *this;
}

VkPushConstantRange PushConstants::Range(VkShaderStageFlags stages) const
{
    VkPushConstantRange r{};
    r.stageFlags = stages;
    r.offset     = 0;
    r.size       = static_cast<uint32_t>(m_Buffer.size());
    return r;
}

void PushConstants::Apply(VkCommandBuffer cb,
                          VkPipelineLayout layout,
                          VkShaderStageFlags stages) const
{
    if (m_Buffer.empty()) return;
    vkCmdPushConstants(cb, layout, stages, 0,
                       static_cast<uint32_t>(m_Buffer.size()),
                       m_Buffer.data());
}


// =============================================================================
// [25] Primitives
// =============================================================================
namespace Primitives {

// Unit cube centred at origin, six faces with per-face normals & UVs.
// 24 vertices (no shared edges so normals stay flat-shaded), 36 indices.
Mesh Cube(float size)
{
    const float h = size * 0.5f;
    Mesh m;
    m.positions = {
        // +X
        { h, -h, -h}, { h,  h, -h}, { h,  h,  h}, { h, -h,  h},
        // -X
        {-h, -h,  h}, {-h,  h,  h}, {-h,  h, -h}, {-h, -h, -h},
        // +Y
        {-h,  h, -h}, {-h,  h,  h}, { h,  h,  h}, { h,  h, -h},
        // -Y
        {-h, -h,  h}, {-h, -h, -h}, { h, -h, -h}, { h, -h,  h},
        // +Z
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h},
        // -Z
        { h, -h, -h}, {-h, -h, -h}, {-h,  h, -h}, { h,  h, -h},
    };
    m.normals.reserve(24);
    for (int i = 0; i < 4; ++i) m.normals.push_back({ 1,  0,  0});
    for (int i = 0; i < 4; ++i) m.normals.push_back({-1,  0,  0});
    for (int i = 0; i < 4; ++i) m.normals.push_back({ 0,  1,  0});
    for (int i = 0; i < 4; ++i) m.normals.push_back({ 0, -1,  0});
    for (int i = 0; i < 4; ++i) m.normals.push_back({ 0,  0,  1});
    for (int i = 0; i < 4; ++i) m.normals.push_back({ 0,  0, -1});

    m.uvs.reserve(24);
    for (int face = 0; face < 6; ++face)
    {
        m.uvs.push_back({0.0f, 1.0f});
        m.uvs.push_back({0.0f, 0.0f});
        m.uvs.push_back({1.0f, 0.0f});
        m.uvs.push_back({1.0f, 1.0f});
    }

    m.indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face)
    {
        const uint32_t base = face * 4;
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 3);
    }
    return m;
}

Mesh Plane(float width, float height)
{
    const float hw = width  * 0.5f;
    const float hh = height * 0.5f;
    Mesh m;
    m.positions = {
        {-hw, 0, -hh}, { hw, 0, -hh}, { hw, 0,  hh}, {-hw, 0,  hh},
    };
    m.normals = {
        {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
    };
    m.uvs = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
    };
    m.indices = { 0, 1, 2, 0, 2, 3 };
    return m;
}

// UV sphere with `rings` latitude bands and `sectors` longitude bands.
// Counts: (rings+1)*(sectors+1) vertices, rings*sectors*6 indices.
Mesh Sphere(float radius, int rings, int sectors)
{
    if (rings   < 2) rings   = 2;
    if (sectors < 3) sectors = 3;

    Mesh m;
    const int vertCount = (rings + 1) * (sectors + 1);
    m.positions.reserve(vertCount);
    m.normals.reserve(vertCount);
    m.uvs.reserve(vertCount);

    for (int r = 0; r <= rings; ++r)
    {
        const float v     = static_cast<float>(r) / static_cast<float>(rings);
        const float phi   = v * 3.14159265358979323846f;                // 0..pi
        const float sinP  = std::sin(phi);
        const float cosP  = std::cos(phi);
        for (int s = 0; s <= sectors; ++s)
        {
            const float u    = static_cast<float>(s) / static_cast<float>(sectors);
            const float thet = u * 6.28318530717958647692f;             // 0..2pi
            const float sinT = std::sin(thet);
            const float cosT = std::cos(thet);

            const Vec3 n { sinP * cosT, cosP, sinP * sinT };
            m.positions.push_back({ radius * n.x, radius * n.y, radius * n.z });
            m.normals.push_back(n);
            m.uvs.push_back({ u, 1.0f - v });
        }
    }

    m.indices.reserve(static_cast<std::size_t>(rings) * sectors * 6);
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < sectors; ++s)
        {
            const uint32_t a = static_cast<uint32_t>( r      * (sectors + 1) + s    );
            const uint32_t b = static_cast<uint32_t>((r + 1) * (sectors + 1) + s    );
            const uint32_t c = static_cast<uint32_t>((r + 1) * (sectors + 1) + s + 1);
            const uint32_t d = static_cast<uint32_t>( r      * (sectors + 1) + s + 1);
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(c);
            m.indices.push_back(a);
            m.indices.push_back(c);
            m.indices.push_back(d);
        }
    }
    return m;
}

Mesh Quad()
{
    // Fullscreen-friendly XY quad, z=0, Y-up.  Drop the projection if you
    // want strict clip-space coordinates; the caller's shader decides.
    Mesh m;
    m.positions = {
        {-1, -1, 0}, { 1, -1, 0}, { 1,  1, 0}, {-1,  1, 0},
    };
    m.normals = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
    };
    m.uvs = {
        {0, 1}, {1, 1}, {1, 0}, {0, 0},
    };
    m.indices = { 0, 1, 2, 0, 2, 3 };
    return m;
}

Mesh Line(const Vec3& a, const Vec3& b)
{
    // Two-vertex mesh.  Caller is responsible for picking
    // VK_PRIMITIVE_TOPOLOGY_LINE_LIST on the pipeline; VCK stays out of
    // pipeline state choices (rule 16).
    Mesh m;
    m.positions = { a, b };
    m.normals   = { {0, 1, 0}, {0, 1, 0} };
    m.uvs       = { {0, 0}, {1, 0} };
    m.indices   = { 0, 1 };
    return m;
}

} // namespace Primitives


// =============================================================================

} // namespace VCK
