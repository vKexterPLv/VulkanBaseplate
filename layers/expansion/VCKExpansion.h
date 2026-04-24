// =============================================================================
//  VCKExpansion.h  -  Expansion layer (rendering building blocks)
//
//  Classes [1]-[12]: VulkanOneTimeCommand, VulkanFramebufferSet,
//  VulkanDepthBuffer, VulkanSampler, VulkanTexture, VulkanMesh,
//  VulkanDescriptorLayoutBuilder, VulkanDescriptorPool, VulkanUniformSet<T>,
//  VulkanDescriptorAllocator, VulkanModelPipeline, VulkanMipmapGenerator,
//  plus VCK::HandleLiveResize free function (base + depth overloads).
//
//  See VCK.h for the full API reference - this file is the structural home
//  of expansion classes, not the documentation source of truth.
// =============================================================================
#pragma once
// Note: VCK.h is already included before this file.

#include <functional>
#include <initializer_list>
#include <memory>

namespace VCK {

// =============================================================================
// [1] VulkanOneTimeCommand
//
//  Submit a single one-off GPU command using the already-existing command pool.
//
//  Usage:
//    VulkanOneTimeCommand otc;
//    otc.Begin(device, command);
//    // record vkCmd* calls via otc.Cmd()
//    otc.End();   // submits, vkQueueWaitIdle, frees buffer
// =============================================================================
class VulkanOneTimeCommand
{
public:
    VulkanOneTimeCommand()  = default;
    ~VulkanOneTimeCommand() = default;

    VulkanOneTimeCommand(const VulkanOneTimeCommand&)            = delete;
    VulkanOneTimeCommand& operator=(const VulkanOneTimeCommand&) = delete;

    // Allocates a command buffer from command.GetCommandPool() and begins recording.
    bool Begin(VulkanDevice& device, VulkanCommand& command);

    // Ends recording, submits to graphics queue, waits idle, frees the buffer.
    void End();

    VkCommandBuffer Cmd() const { return m_Cmd; }

private:
    VulkanDevice*   m_Device = nullptr;
    VkCommandBuffer m_Cmd    = VK_NULL_HANDLE;
    VkCommandPool   m_Pool   = VK_NULL_HANDLE;   // borrowed - NOT owned
};


// =============================================================================
// [2] VulkanFramebufferSet
//
//  Creates one VkFramebuffer per swapchain image, bound to a render pass.
//  Optionally attaches a depth image view as the second attachment.
//
//  Usage:
//    VulkanFramebufferSet fbs;
//    fbs.Initialize(device, swapchain, pipeline.GetRenderPass());
//    // on resize:
//    fbs.Recreate(pipeline.GetRenderPass());
//    // in DrawFrame():
//    rp.framebuffer = fbs.Get(imageIndex);
// =============================================================================
// Forward decl - VulkanDepthBuffer is defined below but some VulkanFramebufferSet
// overloads take it by reference.
class VulkanDepthBuffer;

class VulkanFramebufferSet
{
public:
    VulkanFramebufferSet()  = default;
    ~VulkanFramebufferSet() = default;

    VulkanFramebufferSet(const VulkanFramebufferSet&)            = delete;
    VulkanFramebufferSet& operator=(const VulkanFramebufferSet&) = delete;

    //  Preferred form - pass the VulkanPipeline (and optionally the depth
    //  buffer).  No `.GetRenderPass()` / `.GetImageView()` plumbing at the
    //  call site:
    //      fbs.Initialize(device, swapchain, pipeline);
    //      fbs.Initialize(device, swapchain, pipeline, depth);
    bool Initialize(VulkanDevice&    device,
                    VulkanSwapchain& swapchain,
                    VulkanPipeline&  pipeline);

    bool Initialize(VulkanDevice&     device,
                    VulkanSwapchain&  swapchain,
                    VulkanPipeline&   pipeline,
                    VulkanDepthBuffer& depth);

    //  Raw-handle overload - kept for advanced users.
    bool Initialize(VulkanDevice&    device,
                    VulkanSwapchain& swapchain,
                    VkRenderPass     renderPass,
                    VkImageView      depthView = VK_NULL_HANDLE);

    void Shutdown();

    // Destroys and recreates all framebuffers (call after swapchain.Recreate()).
    bool Recreate(VulkanPipeline& pipeline);
    bool Recreate(VulkanPipeline& pipeline, VulkanDepthBuffer& depth);
    bool Recreate(VkRenderPass renderPass,
                  VkImageView  depthView = VK_NULL_HANDLE);

    VkFramebuffer Get(uint32_t imageIndex) const;
    uint32_t      Count() const { return static_cast<uint32_t>(m_Framebuffers.size()); }

private:
    bool CreateAll(VkRenderPass renderPass, VkImageView depthView);
    void DestroyAll();

    VulkanDevice*              m_Device    = nullptr;
    VulkanSwapchain*           m_Swapchain = nullptr;
    std::vector<VkFramebuffer> m_Framebuffers;
};


// =============================================================================
//  HandleLiveResize  -  one-call resize for resizable windows
//
//  Wires VCK::Window's auto-tracked size + resize latch straight into the
//  swapchain and framebuffer set.  Call once per frame, before DrawFrame():
//
//      while (!window.ShouldClose())
//      {
//          if (window.IsMinimized()) { window.WaitEvents(); continue; }
//          window.PollEvents();
//          VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);
//          DrawFrame();
//      }
//
//  Behaviour:
//    - If !window.WasResized()         -> returns false, no work done.
//    - If window.IsMinimized()         -> returns false (nothing to rebuild).
//    - Else: vkDeviceWaitIdle(device), swapchain.Recreate(w, h),
//            framebuffers.Recreate(pipeline), clears the latch, returns true.
//
//  The overload taking a VulkanDepthBuffer also rebuilds the depth image.
// =============================================================================
class Window;                         // forward decl (defined in VCKCrossplatform.h)

bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline);

bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      VulkanDepthBuffer&    depth);



// =============================================================================
// [3] VulkanDepthBuffer
//
//  Depth image backed by VulkanImage.  Format is chosen automatically
//  (D32_SFLOAT preferred, falling back to D32_SFLOAT_S8_UINT / D24_UNORM_S8_UINT).
//
//  Usage:
//    VulkanDepthBuffer depth;
//    depth.Initialize(device, width, height);
//    // on resize:
//    depth.Recreate(newWidth, newHeight);
//    // pass to framebuffers:
//    fbs.Initialize(device, swapchain, renderPass, depth.GetImageView());
// =============================================================================
class VulkanDepthBuffer
{
public:
    VulkanDepthBuffer()  = default;
    ~VulkanDepthBuffer() = default;

    VulkanDepthBuffer(const VulkanDepthBuffer&)            = delete;
    VulkanDepthBuffer& operator=(const VulkanDepthBuffer&) = delete;

    bool Initialize(VulkanDevice& device, uint32_t width, uint32_t height);
    void Shutdown();
    bool Recreate(uint32_t width, uint32_t height);

    VkImageView GetImageView() const { return m_Image.GetImageView(); }
    VkFormat    GetFormat()    const { return m_Image.GetFormat(); }
    bool        IsValid()      const { return m_Image.IsValid(); }

private:
    VulkanDevice* m_Device = nullptr;
    VulkanImage   m_Image;
};


// =============================================================================
// [4] VulkanSampler
//
//  Thin wrapper around VkSampler.  Factory methods cover the two most common
//  filter modes; destroy with Shutdown().
//
//  Usage:
//    VulkanSampler s;
//    s.CreateLinear(device);
//    // use s.GetSampler() in descriptor writes
//    s.Shutdown();
// =============================================================================
class VulkanSampler
{
public:
    VulkanSampler()  = default;
    ~VulkanSampler() = default;

    VulkanSampler(const VulkanSampler&)            = delete;
    VulkanSampler& operator=(const VulkanSampler&) = delete;

    bool CreateNearest(VulkanDevice& device);
    bool CreateLinear (VulkanDevice& device);
    void Shutdown();

    VkSampler GetSampler() const { return m_Sampler; }
    bool      IsValid()    const { return m_Sampler != VK_NULL_HANDLE; }

private:
    bool Create(VulkanDevice& device, VkFilter filter, VkSamplerMipmapMode mipMode);

    VulkanDevice* m_Device  = nullptr;
    VkSampler     m_Sampler = VK_NULL_HANDLE;
};


// =============================================================================
// [5] VulkanTexture
//
//  GPU image (R8G8B8A8_SRGB) + linear sampler.  CPU pixels are uploaded via
//  a staging buffer + VulkanOneTimeCommand - no manual staging required.
//
//  Usage:
//    VulkanTexture tex;
//    tex.CreateFromPixels(device, command, pixels, width, height);
//    // use tex.GetImageView() and tex.GetSampler() in descriptor writes
//    tex.Shutdown();
// =============================================================================
class VulkanTexture
{
public:
    VulkanTexture()  = default;
    ~VulkanTexture() = default;

    VulkanTexture(const VulkanTexture&)            = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;

    bool CreateFromPixels(VulkanDevice&  device,
                          VulkanCommand& command,
                          const uint8_t* pixels,
                          uint32_t       width,
                          uint32_t       height);
    void Shutdown();

    VkImageView GetImageView() const { return m_Image.GetImageView(); }
    VkSampler   GetSampler()   const { return m_Sampler.GetSampler(); }
    bool        IsValid()      const { return m_Image.IsValid(); }

private:
    VulkanImage   m_Image;
    VulkanSampler m_Sampler;
};


// =============================================================================
// [6] VulkanMesh
//
//  Vertex buffer + optional index buffer uploaded to GPU via staging.
//  Call RecordDraw() inside an open render pass.
//
//  Usage:
//    VulkanMesh mesh;
//    mesh.Upload(device, command, verts, vertBytes, indices, indexCount);
//    // inside render pass:
//    mesh.RecordDraw(cmd);
//    // shutdown before device:
//    mesh.Shutdown();
// =============================================================================
class VulkanMesh
{
public:
    VulkanMesh()  = default;
    ~VulkanMesh() = default;

    VulkanMesh(const VulkanMesh&)            = delete;
    VulkanMesh& operator=(const VulkanMesh&) = delete;

    // vertices / vertexSize : raw vertex data + byte size
    // vertexCount           : number of vertices (required for non-indexed draw)
    // indices / indexCount  : uint32_t index array (pass nullptr / 0 for non-indexed)
    bool Upload(VulkanDevice&   device,
                VulkanCommand&  command,
                const void*     vertices,
                VkDeviceSize    vertexSize,
                uint32_t        vertexCount,
                const uint32_t* indices,
                uint32_t        indexCount);

    // Backwards-compatible overload - assumes indexed draw; derives vertex
    // count only for the indexed path (uses indexCount for vkCmdDrawIndexed).
    // Prefer the form with explicit vertexCount.
    bool Upload(VulkanDevice&   device,
                VulkanCommand&  command,
                const void*     vertices,
                VkDeviceSize    vertexSize,
                const uint32_t* indices,
                uint32_t        indexCount);

    void Shutdown();

    // Binds vertex/index buffers and issues the draw call.
    void RecordDraw(VkCommandBuffer cmd) const;

    bool IsValid() const { return m_VertexBuffer.IsValid(); }

private:
    VulkanBuffer m_VertexBuffer;
    VulkanBuffer m_IndexBuffer;
    uint32_t     m_VertexCount = 0;
    uint32_t     m_IndexCount  = 0;
};


// =============================================================================
// [7] VulkanDescriptorLayoutBuilder
//
//  Fluent builder for VkDescriptorSetLayout.  Caller owns the returned layout
//  and must destroy it with vkDestroyDescriptorSetLayout when done.
//
//  Usage:
//    VkDescriptorSetLayout layout =
//        VulkanDescriptorLayoutBuilder{}
//            .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
//            .Build(device);
// =============================================================================
class VulkanDescriptorLayoutBuilder
{
public:
    VulkanDescriptorLayoutBuilder()  = default;
    ~VulkanDescriptorLayoutBuilder() = default;

    // Appends a single binding and returns *this for chaining.
    VulkanDescriptorLayoutBuilder& Add(uint32_t           binding,
                                       VkDescriptorType   type,
                                       VkShaderStageFlags stages);

    // Creates and returns the layout.  Returns VK_NULL_HANDLE on failure.
    VkDescriptorSetLayout Build(VulkanDevice& device) const;

private:
    std::vector<VkDescriptorSetLayoutBinding> m_Bindings;
};


// =============================================================================
// [8] VulkanDescriptorPool
//
//  Creates a VkDescriptorPool and pre-allocates MAX_FRAMES_IN_FLIGHT descriptor
//  sets from a single layout.
//
//  Usage:
//    VkDescriptorSetLayout layout = VulkanDescriptorLayoutBuilder{}.Add(...).Build(device);
//    VulkanDescriptorPool pool;
//    pool.Initialize(device, layout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
//    VkDescriptorSet set = pool.GetSet(frameIndex);
//    pool.Shutdown();
//    vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr);
// =============================================================================
class VulkanDescriptorPool
{
public:
    VulkanDescriptorPool()  = default;
    ~VulkanDescriptorPool() = default;

    VulkanDescriptorPool(const VulkanDescriptorPool&)            = delete;
    VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;

    bool Initialize(VulkanDevice&         device,
                    VkDescriptorSetLayout layout,
                    VkDescriptorType      type);
    void Shutdown();

    VkDescriptorSet GetSet(uint32_t frameIndex) const { return m_Sets[frameIndex]; }

private:
    VulkanDevice*  m_Device = nullptr;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_Sets{};
};


// =============================================================================
// [9] VulkanUniformSet<T>
//
//  Per-frame typed UBO.  Owns MAX_FRAMES_IN_FLIGHT VulkanBuffers and points
//  descriptor sets (from VulkanDescriptorPool) at them on Initialize().
//
//  Usage:
//    VulkanUniformSet<MyUBO> ubo;
//    ubo.Initialize(device, pool, binding);
//    // every frame:
//    ubo.Write(frameIndex, myData);
//    vkCmdBindDescriptorSets(..., ubo.GetSet(frameIndex), ...);
//    // shutdown before device:
//    ubo.Shutdown();
// =============================================================================
template<typename T>
class VulkanUniformSet
{
public:
    VulkanUniformSet()  = default;
    ~VulkanUniformSet() = default;

    VulkanUniformSet(const VulkanUniformSet&)            = delete;
    VulkanUniformSet& operator=(const VulkanUniformSet&) = delete;

    // pool   - provides the pre-allocated descriptor sets
    // layout - the VkDescriptorSetLayout the sets were allocated from
    // binding - the binding index to write the UBO into
    bool Initialize(VulkanDevice&        device,
                    VulkanDescriptorPool& pool,
                    uint32_t              binding)
    {
        m_Device  = &device;
        m_Binding = binding;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_Sets[i] = pool.GetSet(i);

            if (!m_Buffers[i].CreateUniform(device, sizeof(T)))
                return false;

            // Point descriptor set at the buffer
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = m_Buffers[i].GetBuffer();
            bufInfo.offset = 0;
            bufInfo.range  = sizeof(T);

            VkWriteDescriptorSet write{};
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = m_Sets[i];
            write.dstBinding      = m_Binding;
            write.descriptorCount = 1;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo     = &bufInfo;

            vkUpdateDescriptorSets(device.GetDevice(), 1, &write, 0, nullptr);
        }
        return true;
    }

    void Shutdown()
    {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            m_Buffers[i].Shutdown();
        m_Device = nullptr;
    }

    // Upload CPU data into the current frame's uniform buffer.
    void Write(uint32_t frameIndex, const T& data)
    {
        m_Buffers[frameIndex].Upload(&data, sizeof(T));
    }

    VkDescriptorSet GetSet(uint32_t frameIndex) const { return m_Sets[frameIndex]; }

private:
    VulkanDevice* m_Device  = nullptr;
    uint32_t      m_Binding = 0;
    std::array<VulkanBuffer,    MAX_FRAMES_IN_FLIGHT> m_Buffers{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_Sets{};
};


// =============================================================================
// [10] VulkanDescriptorAllocator
//
//  Creates a VkDescriptorPool with multiple descriptor types and exposes
//  Allocate() to pull individual VkDescriptorSets from any compatible layout.
//  All sets are freed implicitly when Shutdown() destroys the pool.
//
//  Usage:
//    VulkanDescriptorAllocator alloc;
//    alloc.Initialize(device, 8,
//        {{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         4 },
//         { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }});
//    VkDescriptorSet s = alloc.Allocate(layout);
//    // use s...
//    alloc.Shutdown();
// =============================================================================
class VulkanDescriptorAllocator
{
public:
    struct PoolSize {
        VkDescriptorType type;
        uint32_t         count;
    };

    VulkanDescriptorAllocator()  = default;
    ~VulkanDescriptorAllocator() = default;

    VulkanDescriptorAllocator(const VulkanDescriptorAllocator&)            = delete;
    VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;

    bool Initialize(VulkanDevice&                   device,
                    uint32_t                        maxSets,
                    std::initializer_list<PoolSize> sizes);
    void Shutdown();

    // Allocates a single descriptor set from the given layout.
    // Returns VK_NULL_HANDLE on failure.
    VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

private:
    VulkanDevice*    m_Device = nullptr;
    VkDescriptorPool m_Pool   = VK_NULL_HANDLE;
};


// =============================================================================
// [11] VulkanModelPipeline
//
//  A fully-wired graphics pipeline with descriptor set layouts and push constants.
//  VulkanPipeline owns the VkRenderPass and creates a baseline VkPipeline with
//  an empty layout.  VulkanModelPipeline takes that render pass and builds the
//  properly-wired VkPipeline used for actual drawing:
//
//  Descriptor layout:
//    set 0, binding 0 - per-frame UBO          (VK_SHADER_STAGE_VERTEX_BIT)
//    set 1, binding 0 - combined image/sampler (VK_SHADER_STAGE_FRAGMENT_BIT)
//
//  Push constant (VK_SHADER_STAGE_VERTEX_BIT, 64 bytes):
//    mat4 model
//
//  Usage (after VulkanPipeline::Initialize has run):
//    VulkanModelPipeline modelPipeline;
//    modelPipeline.Initialize(device, pipeline.GetRenderPass(), shaders, vertexInput);
//    // draw:
//    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
//                      modelPipeline.GetPipeline());
//    vkCmdBindDescriptorSets(cmd, ..., modelPipeline.GetPipelineLayout(), ...);
//    vkCmdPushConstants(cmd, modelPipeline.GetPipelineLayout(), ...);
//    mesh.RecordDraw(cmd);
//    // shutdown before VulkanPipeline:
//    modelPipeline.Shutdown();
// =============================================================================
class VulkanModelPipeline
{
public:
    VulkanModelPipeline()  = default;
    ~VulkanModelPipeline() = default;

    VulkanModelPipeline(const VulkanModelPipeline&)            = delete;
    VulkanModelPipeline& operator=(const VulkanModelPipeline&) = delete;

    // renderPass - from VulkanPipeline::GetRenderPass() (borrowed, not owned)
    // samples    - must match the render pass's colour attachment samples.
    //              Default is 1x; pass swapchain.GetMSAASamples() when using
    //              an MSAA render pass so the pipeline's multisample state
    //              lines up with the render pass.
    bool Initialize(VulkanDevice&                              device,
                    VkRenderPass                               renderPass,
                    const VulkanPipeline::ShaderInfo&          shaders,
                    const VulkanPipeline::VertexInputInfo&     vertexInput);
    bool Initialize(VulkanDevice&                              device,
                    VkRenderPass                               renderPass,
                    const VulkanPipeline::ShaderInfo&          shaders,
                    const VulkanPipeline::VertexInputInfo&     vertexInput,
                    VkSampleCountFlagBits                      samples);
    void Shutdown();

    // ── Accessors ────────────────────────────────────────────────────────────
    VkPipeline            GetPipeline()       const { return m_Pipeline; }
    VkPipelineLayout      GetPipelineLayout() const { return m_PipelineLayout; }
    VkDescriptorSetLayout GetSet0Layout()     const { return m_Set0Layout; }
    VkDescriptorSetLayout GetSet1Layout()     const { return m_Set1Layout; }

private:
    bool BuildDescriptorLayouts();
    bool BuildPipelineLayout();
    bool BuildGraphicsPipeline(VkRenderPass                              renderPass,
                               const VulkanPipeline::ShaderInfo&         shaders,
                               const VulkanPipeline::VertexInputInfo&    vertexInput,
                               VkSampleCountFlagBits                     samples);
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);

    VulkanDevice*         m_Device         = nullptr;
    VkDescriptorSetLayout m_Set0Layout     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_Set1Layout     = VK_NULL_HANDLE;
    VkPipelineLayout      m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_Pipeline       = VK_NULL_HANDLE;
};


// =============================================================================
// [12] VulkanMipmapGenerator
//
//  Generates a full mip chain for a VkImage by issuing a sequence of
//  vkCmdBlitImage calls - one blit per mip level - inside a single
//  VulkanOneTimeCommand.
//
//  Requirements:
//    • The image must have been created with VK_IMAGE_USAGE_TRANSFER_SRC_BIT
//      AND VK_IMAGE_USAGE_TRANSFER_DST_BIT in addition to its intended usage
//      flags (e.g. VK_IMAGE_USAGE_SAMPLED_BIT).
//    • The physical device must support VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
//      for the image's format (checked by IsFormatSupported()).
//    • mipLevels must be pre-calculated:
//        uint32_t mipLevels = VulkanMipmapGenerator::MipLevels(width, height);
//      and passed to vkImageCreateInfo.mipLevels when the image is created.
//
//  Layout contract:
//    • The image must be in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL before calling Generate().
//    • After Generate() the entire mip chain is left in
//      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (ready to bind as a sampler).
//
//  Usage:
//    // 1. Calculate mip levels before image creation
//    uint32_t mips = VulkanMipmapGenerator::MipLevels(width, height);
//
//    // 2. Create image with TRANSFER_SRC + TRANSFER_DST + SAMPLED usage,
//    //    and mipLevels = mips.
//    //    (Use vkCreateImage directly - VulkanImage wraps a single level;
//    //     mip-aware image creation requires the raw Vulkan call.)
//
//    // 3. Upload base level into the image (staging copy, same as VulkanTexture),
//    //    leaving image in TRANSFER_DST_OPTIMAL.
//
//    // 4. Generate the remaining mip levels:
//    if (VulkanMipmapGenerator::IsFormatSupported(device, VK_FORMAT_R8G8B8A8_SRGB))
//    {
//        VulkanMipmapGenerator gen;
//        gen.Generate(device, command, image, width, height, mips);
//    }
//
//    // Image is now in SHADER_READ_ONLY_OPTIMAL across all mip levels.
//
//  ── Static helpers ────────────────────────────────────────────────────────
//  static uint32_t MipLevels(uint32_t width, uint32_t height)
//    Returns floor(log2(max(width, height))) + 1.
//    This is the standard mip count for a 2-D image.
//  static bool IsFormatSupported(VulkanDevice& device, VkFormat format)
//    Returns true if the physical device supports linear filtering for the
//    given format (required for vkCmdBlitImage with VK_FILTER_LINEAR).
//
//  ── Instance method ───────────────────────────────────────────────────────
//  bool Generate(VulkanDevice&  device,
//                VulkanCommand& command,
//                VkImage        image,
//                uint32_t       width,
//                uint32_t       height,
//                uint32_t       mipLevels)
//    Generates mip levels [1 .. mipLevels-1] via blit, then transitions the
//    entire image to SHADER_READ_ONLY_OPTIMAL.
//    Returns false if the one-time command could not be started.
// =============================================================================
class VulkanMipmapGenerator
{
public:
    VulkanMipmapGenerator()  = default;
    ~VulkanMipmapGenerator() = default;

    VulkanMipmapGenerator(const VulkanMipmapGenerator&)            = delete;
    VulkanMipmapGenerator& operator=(const VulkanMipmapGenerator&) = delete;

    // Returns the number of mip levels needed to reach a 1x1 level.
    static uint32_t MipLevels(uint32_t width, uint32_t height);

    // Returns true if the device supports linear blitting for the given format.
    // Call before Generate() - if false, fall back to nearest or skip mips.
    static bool IsFormatSupported(VulkanDevice& device, VkFormat format);

    // Generates mip levels 1..mipLevels-1 from the already-uploaded level 0.
    // image must be in TRANSFER_DST_OPTIMAL on entry.
    // On return the entire chain is in SHADER_READ_ONLY_OPTIMAL.
    bool Generate(VulkanDevice&  device,
                  VulkanCommand& command,
                  VkImage        image,
                  uint32_t       width,
                  uint32_t       height,
                  uint32_t       mipLevels);
};


// =============================================================================
// [23] VertexLayout
//
//  Named vertex-input builder.  Turns a sequence of Add("name", type) calls
//  into the two VkVertex* structs `VulkanModelPipeline::Initialize` expects.
//  Location indices are assigned in insertion order starting from 0.  See
//  VCK.h for the canonical docs.
// =============================================================================
enum class VertexAttrType : uint8_t
{
    Float,
    Vec2,
    Vec3,
    Vec4,
    Int,
    UInt,
};

class VertexLayout
{
public:
    VertexLayout& Add(const char* name, VertexAttrType t);

    uint32_t                                       Stride()     const { return m_Stride; }
    VkVertexInputBindingDescription                Binding(uint32_t binding = 0) const;
    std::vector<VkVertexInputAttributeDescription> Attributes(uint32_t binding = 0) const;

    std::size_t Count() const { return m_Attrs.size(); }

private:
    struct Entry
    {
        const char*    name;    // caller-owned literal
        VertexAttrType type;
        uint32_t       offset;
        uint32_t       size;
        VkFormat       format;
    };

    std::vector<Entry> m_Attrs;
    uint32_t           m_Stride = 0;
};


// =============================================================================
// [24] PushConstants
//
//  Named push-constant block.  `Declare(name, type)` reserves the slot and
//  records its byte offset.  `Set(name, value)` does a direct memcpy into
//  the backing buffer - no hashing on the hot path, no std::any, no
//  reflection.  `Apply(cb, layout, stages)` forwards one vkCmdPushConstants
//  call covering the whole block.  See VCK.h for the full API.
// =============================================================================
enum class PushConstType : uint8_t
{
    Float,
    Vec2,
    Vec3,
    Vec4,
    Mat4,
    Int,
    UInt,
};

class PushConstants
{
public:
    PushConstants& Declare(const char* name, PushConstType t);

    PushConstants& Set(const char* name, float v);
    PushConstants& Set(const char* name, const Vec2& v);
    PushConstants& Set(const char* name, const Vec3& v);
    PushConstants& Set(const char* name, const Vec4& v);
    PushConstants& Set(const char* name, const Mat4& v);
    PushConstants& Set(const char* name, int32_t v);
    PushConstants& Set(const char* name, uint32_t v);

    // Total size of the declared block, in bytes.
    uint32_t Size() const { return static_cast<uint32_t>(m_Buffer.size()); }

    // Build the VkPushConstantRange for a pipeline layout.  `stages` is
    // typically VK_SHADER_STAGE_VERTEX_BIT or VK_SHADER_STAGE_ALL_GRAPHICS.
    VkPushConstantRange Range(VkShaderStageFlags stages) const;

    // Upload the whole block to the command buffer.  Caller must have
    // recorded a BindPipeline that uses `layout`.
    void Apply(VkCommandBuffer cb,
               VkPipelineLayout layout,
               VkShaderStageFlags stages) const;

private:
    struct Slot
    {
        const char*   name;     // caller-owned literal
        PushConstType type;
        uint32_t      offset;
        uint32_t      size;
    };

    // Returns pointer into m_Buffer at the slot's offset, or nullptr on
    // type mismatch / unknown name.  Logs via VCKLog::Error on failure
    // (rule 14).  `expected` is the type the caller is Set()ing with.
    uint8_t* SlotWrite(const char* name, PushConstType expected, uint32_t expectedSize);

    std::vector<Slot>   m_Slots;
    std::vector<uint8_t> m_Buffer;   // packed raw bytes
};


// =============================================================================
// [25] Primitives
//
//  Tiny library of mesh builders that fill a CPU-side Mesh struct.  Pair
//  with VulkanMesh to upload.  VCK never owns the returned Mesh (rule 22).
//  See VCK.h for what each builder produces.
// =============================================================================
namespace Primitives
{
    struct Mesh
    {
        std::vector<Vec3>     positions;
        std::vector<Vec3>     normals;
        std::vector<Vec2>     uvs;
        std::vector<uint16_t> indices;
    };

    Mesh Cube  (float size = 1.0f);
    Mesh Plane (float width = 1.0f, float height = 1.0f);
    Mesh Sphere(float radius = 0.5f, int rings = 16, int sectors = 32);
    Mesh Quad  ();
    Mesh Line  (const Vec3& a, const Vec3& b);
} // namespace Primitives


// =============================================================================

} // namespace VCK
