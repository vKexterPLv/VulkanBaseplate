#include "VCK.h"
// VCKExpansion.h is included at the bottom of VCK.h

#include <algorithm>
#include <iterator>

namespace VCK {

// =============================================================================
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

    return VK_CHECK(vkBeginCommandBuffer(m_Cmd, &bi));
}

void VulkanOneTimeCommand::End()
{
    if (!m_Cmd) return;

    VK_CHECK(vkEndCommandBuffer(m_Cmd));

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_Cmd;

    VK_CHECK(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_Device->GetGraphicsQueue()));

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

    vkDeviceWaitIdle(device.GetDevice());
    if (!swapchain.Recreate(w, h))          { window.ClearResized(); return false; }
    if (!framebuffers.Recreate(pipeline))   { window.ClearResized(); return false; }
    window.ClearResized();
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

    vkDeviceWaitIdle(device.GetDevice());
    if (!swapchain.Recreate(w, h))                 { window.ClearResized(); return false; }
    if (!depth.Recreate(w, h))                     { window.ClearResized(); return false; }
    if (!framebuffers.Recreate(pipeline, depth))   { window.ClearResized(); return false; }
    window.ClearResized();
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

    LogVk("VulkanModelPipeline initialized");
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
    LogVk("VulkanModelPipeline shut down");
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
//  EXECUTION & ORCHESTRATION LAYER  -  implementations
// =============================================================================


// -----------------------------------------------------------------------------
// [14] TimelineSemaphore
// -----------------------------------------------------------------------------
bool TimelineSemaphore::Initialize(VulkanDevice& device, uint64_t initialValue)
{
    m_Device = &device;

    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue  = initialValue;

    VkSemaphoreCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    ci.pNext = &typeInfo;

    const VkResult r = vkCreateSemaphore(device.GetDevice(), &ci, nullptr, &m_Sem);
    if (r != VK_SUCCESS)
    {
        LogVk("[TimelineSemaphore] vkCreateSemaphore failed (VkResult=" +
              std::to_string(static_cast<int>(r)) +
              "). The device was likely not created with timelineSemaphore=VK_TRUE; "
              "fall back to VulkanSync.");
        m_Sem = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void TimelineSemaphore::Shutdown()
{
    if (m_Sem != VK_NULL_HANDLE && m_Device != nullptr)
    {
        vkDestroySemaphore(m_Device->GetDevice(), m_Sem, nullptr);
    }
    m_Sem    = VK_NULL_HANDLE;
    m_Device = nullptr;
}

uint64_t TimelineSemaphore::LastSignaledValue() const
{
    if (!IsValid()) return 0;
    uint64_t v = 0;
    const VkResult r = vkGetSemaphoreCounterValue(m_Device->GetDevice(), m_Sem, &v);
    (void)r;
    return v;
}

bool TimelineSemaphore::Wait(uint64_t value, uint64_t timeoutNs) const
{
    if (!IsValid()) return true;
    VkSemaphoreWaitInfo wi{};
    wi.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wi.semaphoreCount = 1;
    wi.pSemaphores    = &m_Sem;
    wi.pValues        = &value;
    return vkWaitSemaphores(m_Device->GetDevice(), &wi, timeoutNs) == VK_SUCCESS;
}

bool TimelineSemaphore::Signal(uint64_t value)
{
    if (!IsValid()) return false;
    VkSemaphoreSignalInfo si{};
    si.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    si.semaphore = m_Sem;
    si.value     = value;
    return vkSignalSemaphore(m_Device->GetDevice(), &si) == VK_SUCCESS;
}


// -----------------------------------------------------------------------------
// [16] QueueSet
// -----------------------------------------------------------------------------
bool QueueSet::Initialize(VulkanDevice& device)
{
    m_Graphics       = device.GetGraphicsQueue();
    m_GraphicsFamily = device.GetQueueFamilyIndices().GraphicsFamily.value_or(0);

    // VCK's current VulkanDevice only creates a graphics queue.  Until
    // VulkanDevice grows a dedicated compute / transfer queue, both slots
    // alias the graphics queue.  This is legal for vkQueueSubmit - it just
    // means no real queue-level parallelism is achieved.
    m_Compute        = m_Graphics;
    m_ComputeFamily  = m_GraphicsFamily;
    m_Transfer       = m_Graphics;
    m_TransferFamily = m_GraphicsFamily;

    return m_Graphics != VK_NULL_HANDLE;
}


// -----------------------------------------------------------------------------
// [17] GpuSubmissionBatcher
// -----------------------------------------------------------------------------
bool GpuSubmissionBatcher::Initialize(VulkanDevice& device, QueueSet& queues)
{
    m_Device = &device;
    m_Queues = &queues;
    return true;
}

void GpuSubmissionBatcher::Shutdown()
{
    DiscardAll();
    m_Device = nullptr;
    m_Queues = nullptr;
}

void GpuSubmissionBatcher::QueueGraphics(VkCommandBuffer cmd, const SubmitInfo& info)
{
    m_Graphics.push_back({cmd, info});
}

void GpuSubmissionBatcher::QueueCompute(VkCommandBuffer cmd, const SubmitInfo& info)
{
    m_Compute.push_back({cmd, info});
}

void GpuSubmissionBatcher::QueueTransfer(VkCommandBuffer cmd, const SubmitInfo& info)
{
    m_Transfer.push_back({cmd, info});
}

void GpuSubmissionBatcher::FlushAll(VkFence graphicsFence)
{
    if (m_Queues == nullptr) return;

    // Transfer → Compute → Graphics.  Graphics is submitted last with the
    // in-flight fence so the CPU can track retirement of the whole frame.
    FlushQueue(m_Queues->Transfer(), m_Transfer, VK_NULL_HANDLE);
    FlushQueue(m_Queues->Compute(),  m_Compute,  VK_NULL_HANDLE);
    FlushQueue(m_Queues->Graphics(), m_Graphics, graphicsFence);
}

void GpuSubmissionBatcher::DiscardAll()
{
    m_Graphics.clear();
    m_Compute.clear();
    m_Transfer.clear();
}

void GpuSubmissionBatcher::FlushQueue(VkQueue q, std::vector<Entry>& bucket, VkFence fence)
{
    if (bucket.empty())
    {
        if (fence != VK_NULL_HANDLE)
        {
            // Still need to signal the fence so the CPU doesn't deadlock on it
            // next cycle.  Empty submit with no cmds just signals the fence.
            VkSubmitInfo emptyInfo{};
            emptyInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            VK_CHECK(vkQueueSubmit(q, 1, &emptyInfo, fence));
        }
        return;
    }

    std::vector<VkSubmitInfo> submits;
    submits.reserve(bucket.size());
    for (const Entry& e : bucket)
    {
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &e.cmd;

        if (e.info.waitSem != VK_NULL_HANDLE)
        {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores    = &e.info.waitSem;
            si.pWaitDstStageMask  = &e.info.waitStage;
        }
        if (e.info.signalSem != VK_NULL_HANDLE)
        {
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores    = &e.info.signalSem;
        }
        submits.push_back(si);
    }

    VK_CHECK(vkQueueSubmit(q,
                           static_cast<uint32_t>(submits.size()),
                           submits.data(),
                           fence));
    bucket.clear();
}


// -----------------------------------------------------------------------------
// [18] BackpressureGovernor
// -----------------------------------------------------------------------------
void BackpressureGovernor::Initialize(FramePolicy policy, uint32_t maxLag, uint32_t framesInFlight)
{
    m_Policy = policy;

    // Clamp maxLag to the actual number of frames the sync layer can track.
    // The slot-fence mechanism already caps real CPU/GPU lag at that depth,
    // so accepting a larger value would give the user a number Lag() can
    // never actually reach.  Log when clamping so the user can correct cfg.
    if (framesInFlight == 0) framesInFlight = 1;
    if (framesInFlight > MAX_FRAMES_IN_FLIGHT) framesInFlight = MAX_FRAMES_IN_FLIGHT;

    uint32_t clamped = maxLag == 0 ? 1u : maxLag;
    if (clamped > framesInFlight)
    {
        LogVk(std::string("[BackpressureGovernor] asyncMaxLag=") +
              std::to_string(maxLag) +
              " exceeds framesInFlight=" +
              std::to_string(framesInFlight) +
              " - clamped.  Deeper pipelining requires timeline semaphores.");
        clamped = framesInFlight;
    }
    m_MaxLag = clamped;

    m_CpuFrame.store(0);
    m_GpuFrame.store(0);
}

void BackpressureGovernor::Shutdown()
{
    // Nothing to do - no threads, no handles.  All state is plain atomics.
}

void BackpressureGovernor::NoteCpuFrameStart(uint64_t absoluteFrame)
{
    m_CpuFrame.store(absoluteFrame, std::memory_order_release);
}

void BackpressureGovernor::NoteGpuFrameRetired(uint64_t absoluteFrame)
{
    uint64_t prev = m_GpuFrame.load(std::memory_order_acquire);
    // Monotonic update.
    while (absoluteFrame > prev &&
           !m_GpuFrame.compare_exchange_weak(prev, absoluteFrame,
                                             std::memory_order_acq_rel))
    {
        /* retry */
    }
}

// Non-blocking overrun check.
//
// Returns true when the CPU is more than `maxLag` frames ahead of the last
// retired GPU frame.  FrameScheduler is responsible for any actual waiting
// (it has the fences / device that can unstick the lag); the governor itself
// never blocks.  This used to wait on a condition variable, but every caller
// of NoteGpuFrameRetired runs on the render thread too - so the CV would
// self-deadlock the moment the CPU overran.
bool BackpressureGovernor::IsOverrun() const
{
    if (m_Policy != FramePolicy::AsyncMax) return false;
    const uint64_t cpu = m_CpuFrame.load(std::memory_order_acquire);
    const uint64_t gpu = m_GpuFrame.load(std::memory_order_acquire);
    return cpu > gpu && (cpu - gpu) > static_cast<uint64_t>(m_MaxLag);
}

// -----------------------------------------------------------------------------
// [19] JobGraph
// -----------------------------------------------------------------------------
void JobGraph::Initialize(uint32_t workerCount)
{
    if (!m_Workers.empty()) return;  // already initialised

    uint32_t n = workerCount;
    if (n == 0)
    {
        n = std::thread::hardware_concurrency();
        if (n == 0) n = 2;
    }
    if (n > 32) n = 32;

    m_Exit.store(false);
    m_Workers.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        m_Workers.emplace_back([this] { WorkerLoop(); });
    }
}

void JobGraph::Shutdown()
{
    if (m_Workers.empty()) return;

    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_Exit.store(true);
    }
    m_CvWork.notify_all();
    for (std::thread& t : m_Workers)
    {
        if (t.joinable()) t.join();
    }
    m_Workers.clear();
    m_Jobs.clear();
    m_Ready.clear();
    m_Outstanding.store(0);
    m_Executing = false;
}

JobGraph::JobId JobGraph::Add(const char* name, Fn fn, std::initializer_list<JobId> deps)
{
    auto job = std::make_unique<Job>();
    job->id   = static_cast<JobId>(m_Jobs.size());
    job->name = name != nullptr ? name : "job";
    job->fn   = std::move(fn);

    // Count only *valid* deps.  A stale JobId from a previous frame (after
    // Reset()) or a bogus id would otherwise inflate pendingDeps and leave
    // the job unreachable - Execute() would deadlock on the threaded path
    // (m_Outstanding never reaches 0) or silently drop the job + all its
    // dependents on the inline fallback.
    uint32_t validDeps = 0;
    for (JobId dep : deps)
    {
        if (dep < m_Jobs.size())
        {
            m_Jobs[dep]->dependents.push_back(job->id);
            ++validDeps;
        }
        else
        {
            LogVk(std::string("[JobGraph] Add('") +
                  (name != nullptr ? name : "job") +
                  "'): ignoring invalid dep " + std::to_string(dep));
        }
    }
    job->pendingDeps.store(validDeps);

    const JobId id = job->id;
    m_Jobs.push_back(std::move(job));
    return id;
}

void JobGraph::Execute()
{
    if (m_Jobs.empty()) return;
    if (m_Workers.empty())
    {
        // Graceful fallback: run inline on caller thread.
        // Use a simple ready queue; this is not parallel but is correct.
        std::vector<JobId> ready;
        ready.reserve(m_Jobs.size());
        for (auto& j : m_Jobs)
        {
            if (j->pendingDeps.load() == 0) ready.push_back(j->id);
        }
        while (!ready.empty())
        {
            const JobId id = ready.back();
            ready.pop_back();
            Job& j = *m_Jobs[id];
            if (j.fn) j.fn();
            j.done.store(true);
            for (JobId dep : j.dependents)
            {
                if (m_Jobs[dep]->pendingDeps.fetch_sub(1) == 1)
                {
                    ready.push_back(dep);
                }
            }
        }
        return;
    }

    m_Outstanding.store(static_cast<uint32_t>(m_Jobs.size()));
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_Executing = true;
        m_Ready.clear();
        for (auto& j : m_Jobs)
        {
            if (j->pendingDeps.load() == 0) m_Ready.push_back(j->id);
        }
    }
    m_CvWork.notify_all();

    std::unique_lock<std::mutex> lk(m_Mu);
    m_CvDone.wait(lk, [this] {
        return m_Outstanding.load() == 0 || m_Exit.load();
    });
    m_Executing = false;
}

void JobGraph::Reset()
{
    std::lock_guard<std::mutex> lk(m_Mu);
    m_Jobs.clear();
    m_Ready.clear();
    m_Outstanding.store(0);
}

void JobGraph::WorkerLoop()
{
    for (;;)
    {
        JobId id = 0;
        {
            std::unique_lock<std::mutex> lk(m_Mu);
            m_CvWork.wait(lk, [this] { return !m_Ready.empty() || m_Exit.load(); });
            if (m_Exit.load() && m_Ready.empty()) return;

            id = m_Ready.back();
            m_Ready.pop_back();
        }

        Job& j = *m_Jobs[id];
        if (j.fn)
        {
            // User functions are expected to be noexcept; if they throw, we
            // let it propagate rather than silently swallow (matches VCK's
            // fail-loud policy).  In release builds g++ -fno-exceptions makes
            // this a no-op wrapping cost.
            j.fn();
        }
        FinishJob(id);
    }
}

void JobGraph::EnqueueReady(JobId id)
{
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_Ready.push_back(id);
    }
    m_CvWork.notify_one();
}

void JobGraph::FinishJob(JobId id)
{
    Job& j = *m_Jobs[id];
    j.done.store(true);
    for (JobId dep : j.dependents)
    {
        if (m_Jobs[dep]->pendingDeps.fetch_sub(1) == 1)
        {
            EnqueueReady(dep);
        }
    }
    if (m_Outstanding.fetch_sub(1) == 1)
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_CvDone.notify_all();
    }
}


// -----------------------------------------------------------------------------
// [20] DebugTimeline
// -----------------------------------------------------------------------------
void DebugTimeline::Initialize(bool enabled)
{
    m_Enabled = enabled;
    m_Origin  = std::chrono::steady_clock::now();
    m_Spans.clear();
    m_OpenCpu.clear();
}

void DebugTimeline::Shutdown()
{
    m_Enabled = false;
    m_Spans.clear();
    m_OpenCpu.clear();
}

uint64_t DebugTimeline::NowUs() const
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_Origin).count());
}

void DebugTimeline::BeginCpuSpan(const char* name, uint64_t frame)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    Span s;
    s.name    = name ? name : "cpu";
    s.track   = "CPU";
    s.frame   = frame;
    s.startUs = NowUs();
    s.endUs   = 0;
    m_OpenCpu.push_back(std::move(s));
}

void DebugTimeline::EndCpuSpan(const char* name, uint64_t frame)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    // Pop most recent open span with matching name+frame.
    for (auto it = m_OpenCpu.rbegin(); it != m_OpenCpu.rend(); ++it)
    {
        if (it->frame == frame && it->name == (name ? name : "cpu"))
        {
            it->endUs = NowUs();
            m_Spans.push_back(*it);
            m_OpenCpu.erase(std::next(it).base());
            return;
        }
    }
}

void DebugTimeline::RecordGpuSpan(const char* name, uint64_t frame,
                                  uint64_t startUs, uint64_t endUs)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    Span s;
    s.name    = name ? name : "gpu";
    s.track   = "GPU";
    s.frame   = frame;
    s.startUs = startUs;
    s.endUs   = endUs;
    m_Spans.push_back(std::move(s));
}

void DebugTimeline::NoteStall(const char* reason, uint64_t frame, uint64_t durationUs)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    Span s;
    // NoteStall is called *after* the stall has completed, so NowUs() is
    // the end of the stall, not its beginning.  Anchor the span backwards
    // by durationUs so Dump() lines it up with the surrounding CPU spans.
    const uint64_t now = NowUs();
    s.name    = reason ? reason : "stall";
    s.track   = "STALL";
    s.frame   = frame;
    s.startUs = now >= durationUs ? now - durationUs : 0;
    s.endUs   = now;
    m_Spans.push_back(std::move(s));
}

void DebugTimeline::Dump()
{
    if (!m_Enabled) return;
    std::vector<Span> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        snapshot.swap(m_Spans);
    }
    std::sort(snapshot.begin(), snapshot.end(),
              [](const Span& a, const Span& b) { return a.startUs < b.startUs; });

    LogVk("[DebugTimeline] " + std::to_string(snapshot.size()) + " spans:");
    for (const Span& s : snapshot)
    {
        const uint64_t dur = s.endUs > s.startUs ? s.endUs - s.startUs : 0;
        LogVk(std::string("  f=") + std::to_string(s.frame) +
              " [" + s.track + "] " + s.name +
              " @" + std::to_string(s.startUs) + "us  dur=" +
              std::to_string(dur) + "us");
    }
}

void DebugTimeline::ResetBuffer()
{
    std::lock_guard<std::mutex> lk(m_Mu);
    m_Spans.clear();
    m_OpenCpu.clear();
}


// -----------------------------------------------------------------------------
// [22] FrameScheduler
// -----------------------------------------------------------------------------
bool FrameScheduler::Initialize(VulkanDevice&  device,
                                VulkanCommand& command,
                                VulkanSync&    sync,
                                Config         cfg)
{
    m_Device  = &device;
    m_Command = &command;
    m_Sync    = &sync;
    m_Cfg     = cfg;

    if (!m_Queues.Initialize(device))
    {
        LogVk("[FrameScheduler] QueueSet::Initialize failed.");
        return false;
    }
    if (!m_Submissions.Initialize(device, m_Queues))
    {
        LogVk("[FrameScheduler] GpuSubmissionBatcher::Initialize failed.");
        return false;
    }
    // Runtime framesInFlight comes from VulkanSync (already clamped in its
    // Initialize).  Every loop over slots below uses this count, not the
    // compile-time bound.
    m_FramesInFlight = sync.GetFramesInFlight();
    if (m_FramesInFlight == 0) m_FramesInFlight = 1;
    if (m_FramesInFlight > MAX_FRAMES_IN_FLIGHT) m_FramesInFlight = MAX_FRAMES_IN_FLIGHT;

    m_Governor.Initialize(cfg.policy, cfg.asyncMaxLag, m_FramesInFlight);
    m_Timeline.Initialize(cfg.enableTimeline);

    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        m_Jobs[i].Initialize(cfg.jobWorkers);

        Frame& f          = m_Frames[i];
        f.m_Slot          = i;
        f.m_Absolute      = 0;
        f.m_Policy        = cfg.policy;
        f.m_Submissions   = &m_Submissions;
        f.m_Jobs          = &m_Jobs[i];

        m_SlotAbsolute[i] = 0;
    }

    m_Absolute = 0;
    m_InFrame  = false;

    LogVk(std::string("[FrameScheduler] policy=") + FramePolicyName(cfg.policy) +
          " maxLag="   + std::to_string(cfg.asyncMaxLag) +
          " workers="  + std::to_string(m_Jobs[0].WorkerCount()) +
          " timeline=" + (cfg.enableTimeline ? "on" : "off"));

    return true;
}

void FrameScheduler::Shutdown()
{
    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        m_Jobs[i].Shutdown();
    }
    m_Timeline.Shutdown();
    m_Governor.Shutdown();
    m_Submissions.Shutdown();
    m_Queues.Shutdown();

    m_Device  = nullptr;
    m_Command = nullptr;
    m_Sync    = nullptr;
    m_InFrame = false;
}

uint32_t FrameScheduler::CurrentSlot() const
{
    return m_Sync != nullptr ? m_Sync->GetCurrentFrameIndex() : 0u;
}

void FrameScheduler::WaitInFlightFence(uint32_t slot)
{
    if (m_Sync == nullptr || m_Device == nullptr) return;
    VkFence fence = m_Sync->GetInFlightFence(slot);
    if (fence == VK_NULL_HANDLE) return;

    const auto t0 = std::chrono::steady_clock::now();
    VK_CHECK(vkWaitForFences(m_Device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_Device->GetDevice(), 1, &fence));
    const auto t1 = std::chrono::steady_clock::now();

    // Anything the CPU spent blocking is a stall.
    const uint64_t waitedUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    // When a slot's fence clears, the GPU has finished the frame previously
    // assigned to that slot.  That is the most precise retirement signal we
    // can get without timeline semaphores.
    if (m_SlotAbsolute[slot] != 0)
    {
        m_Governor.NoteGpuFrameRetired(m_SlotAbsolute[slot]);
    }
    if (waitedUs > 100 && m_Timeline.Enabled())
    {
        m_Timeline.NoteStall("fence-wait", m_Absolute, waitedUs);
    }
}

void FrameScheduler::RetireCompletedFrames()
{
    // Non-blocking probe: for each slot whose fence is signalled, mark its
    // absolute frame as retired.  Cheap - just a device-side query.
    if (m_Device == nullptr || m_Sync == nullptr) return;
    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        if (m_SlotAbsolute[i] == 0) continue;
        VkFence f = m_Sync->GetInFlightFence(i);
        if (f == VK_NULL_HANDLE) continue;
        if (vkGetFenceStatus(m_Device->GetDevice(), f) == VK_SUCCESS)
        {
            m_Governor.NoteGpuFrameRetired(m_SlotAbsolute[i]);
        }
    }
}

Frame& FrameScheduler::BeginFrame()
{
    ++m_Absolute;
    m_Governor.NoteCpuFrameStart(m_Absolute);

    // Opportunistic retirement probe - cheap, non-blocking fence status query.
    RetireCompletedFrames();

    const uint32_t slot = CurrentSlot();

    // Wait for this slot's fence before reusing its command buffer.
    //
    // All three policies share this single wait:
    //   Lockstep   : fence is still-signalled from its prior EndFrame's wait
    //                (that wait intentionally does NOT reset); the wait here
    //                is therefore a no-op and resets in preparation for the
    //                next submit.
    //   Pipelined  : standard case - block if the GPU hasn't finished this
    //                slot's previous frame yet.
    //   AsyncMax   : same as Pipelined.  The slot-fence mechanism already
    //                caps CPU-GPU lag at MAX_FRAMES_IN_FLIGHT; configuring
    //                asyncMaxLag higher than that is fine (IsOverrun never
    //                fires), configuring it lower is accepted but only
    //                observable via Governor().Lag() - the scheduler does
    //                NOT force extra waits beyond the slot fence (a naive
    //                extra wait risks blocking on an un-submitted fence).
    //                If you need tighter than MAX_FRAMES_IN_FLIGHT, use
    //                FramePolicy::Lockstep instead.
    WaitInFlightFence(slot);

    // Reset this slot's job graph.
    m_Jobs[slot].Reset();

    // Reset the slot's command buffer and open it for recording.
    if (m_Command != nullptr)
    {
        m_Command->BeginRecording(slot);
    }

    Frame& f = m_Frames[slot];
    f.m_Slot            = slot;
    f.m_Absolute        = m_Absolute;
    f.m_Policy          = m_Cfg.policy;
    f.m_Fence           = m_Sync ? m_Sync->GetInFlightFence(slot)            : VK_NULL_HANDLE;
    f.m_ImageAvailable  = m_Sync ? m_Sync->GetImageAvailableSemaphore(slot)  : VK_NULL_HANDLE;
    f.m_RenderFinished  = m_Sync ? m_Sync->GetRenderFinishedSemaphore(slot)  : VK_NULL_HANDLE;
    f.m_PrimaryCmd      = m_Command ? m_Command->GetCommandBuffer(slot)      : VK_NULL_HANDLE;
    f.m_Submissions     = &m_Submissions;
    f.m_Jobs            = &m_Jobs[slot];

    m_SlotAbsolute[slot] = m_Absolute;
    m_InFrame = true;

    if (m_Timeline.Enabled())
    {
        m_Timeline.BeginCpuSpan("frame", m_Absolute);
    }
    return f;
}

void FrameScheduler::DispatchJobs()
{
    const uint32_t slot = CurrentSlot();
    if (m_Timeline.Enabled()) m_Timeline.BeginCpuSpan("jobs", m_Absolute);
    m_Jobs[slot].Execute();
    if (m_Timeline.Enabled()) m_Timeline.EndCpuSpan("jobs", m_Absolute);
}

void FrameScheduler::EndFrame()
{
    if (!m_InFrame) return;

    const uint32_t slot  = CurrentSlot();
    VkFence        fence = m_Sync ? m_Sync->GetInFlightFence(slot) : VK_NULL_HANDLE;

    // Close the primary command buffer (caller was expected to fill it).
    if (m_Command != nullptr)
    {
        m_Command->EndRecording(slot);
    }

    // Flush all batched submits.  Graphics queue gets the fence.
    m_Submissions.FlushAll(fence);

    // Advance VulkanSync's internal frame index.
    if (m_Sync != nullptr)
    {
        m_Sync->AdvanceFrame();
    }

    if (m_Timeline.Enabled())
    {
        m_Timeline.EndCpuSpan("frame", m_Absolute);
    }

    // Lockstep: block until this frame's GPU work retires.  We wait but do
    // NOT reset - the fence stays signalled until the next BeginFrame reaches
    // this slot, which does the reset inside WaitInFlightFence.  Resetting
    // here would leave the fence in a state where the next same-slot
    // BeginFrame hangs on a never-submitted fence.
    if (m_Cfg.policy == FramePolicy::Lockstep && fence != VK_NULL_HANDLE && m_Device != nullptr)
    {
        VK_CHECK(vkWaitForFences(m_Device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
        m_Governor.NoteGpuFrameRetired(m_Absolute);
    }

    m_InFrame = false;
}


} // namespace VCK
