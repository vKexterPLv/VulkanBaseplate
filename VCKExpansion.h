// =============================================================================
//  VCKExpansion.h
//
//  Arm extension for VCK.h.
//  Included automatically at the bottom of VCK.h —
//  you never need to include this file directly.
//
//  DESIGN RULE
//  ───────────
//  Every class here receives the base objects (VulkanDevice, VulkanSwapchain,
//  etc.) by reference.  They do NOT own, init, or shut down base objects.
//  The original init/shutdown order is unchanged:
//
//    Init:     Context → Device → Swapchain → Pipeline → Command → Sync
//    Shutdown: Sync → Command → Pipeline → Swapchain → Device → Context
//
//  Expansion objects must be shut down BEFORE the base objects.
//
//  CLASSES  —  RENDERING BUILDING BLOCKS
//  ─────────────────────────────────────
//  [1]  VulkanOneTimeCommand          — one-shot GPU command using the existing pool
//  [2]  VulkanFramebufferSet          — per-swapchain-image VkFramebuffers
//  [3]  VulkanDepthBuffer             — depth image (wraps VulkanImage)
//  [4]  VulkanSampler                 — VkSampler factory (nearest / linear)
//  [5]  VulkanTexture                 — VulkanImage + VulkanSampler, staging upload
//  [6]  VulkanMesh                    — vertex + index VulkanBuffers, RecordDraw()
//  [7]  VulkanDescriptorLayoutBuilder — fluent VkDescriptorSetLayout builder
//  [8]  VulkanDescriptorPool          — VkDescriptorPool + per-frame set allocation
//  [9]  VulkanUniformSet<T>           — per-frame typed UBO with Write() + GetSet()
//  [10] VulkanDescriptorAllocator     — mixed-type descriptor pool, per-set Allocate()
//  [11] VulkanModelPipeline           — full model pipeline with UBO layouts + push constants
//  [12] VulkanMipmapGenerator         — blit-based mip chain generation for any VkImage
//
//  CLASSES  —  EXECUTION & ORCHESTRATION
//  ─────────────────────────────────────
//  [13] FramePolicy                   — enum: Lockstep / Pipelined / AsyncMax
//  [14] TimelineSemaphore             — VK_KHR_timeline_semaphore wrapper (opt-in)
//  [15] DependencyToken               — (TimelineSemaphore*, value) — ordering without fences
//  [16] QueueSet                      — graphics / compute / transfer queue handles
//  [17] GpuSubmissionBatcher          — per-queue command-buffer batching, single-flush
//  [18] BackpressureGovernor          — stalls CPU if it outruns GPU past maxLag
//  [19] JobGraph                      — CPU thread-pool + dependency DAG, frame-scoped
//  [20] DebugTimeline                 — text-mode CPU/GPU span recorder + stall log
//  [21] Frame                         — per-frame work container (slot, fence, batchers, jobs)
//  [22] FrameScheduler                — top-level orchestrator — owns lifecycle + policy
//
//  See VCK.h (top of file) for the big-picture layer diagram.
// =============================================================================

#pragma once
// Note: VCK.h is already included before this file.
// All base types (VulkanDevice, VulkanBuffer, VulkanImage, etc.) are available.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <thread>

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
    VkCommandPool   m_Pool   = VK_NULL_HANDLE;   // borrowed — NOT owned
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
class VulkanFramebufferSet
{
public:
    VulkanFramebufferSet()  = default;
    ~VulkanFramebufferSet() = default;

    VulkanFramebufferSet(const VulkanFramebufferSet&)            = delete;
    VulkanFramebufferSet& operator=(const VulkanFramebufferSet&) = delete;

    //  Preferred form — pass the VulkanPipeline (and optionally the depth
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

    //  Raw-handle overload — kept for advanced users.
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
//  a staging buffer + VulkanOneTimeCommand — no manual staging required.
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

    // Backwards-compatible overload — assumes indexed draw; derives vertex
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

    // pool   — provides the pre-allocated descriptor sets
    // layout — the VkDescriptorSetLayout the sets were allocated from
    // binding — the binding index to write the UBO into
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
//    set 0, binding 0 — per-frame UBO          (VK_SHADER_STAGE_VERTEX_BIT)
//    set 1, binding 0 — combined image/sampler (VK_SHADER_STAGE_FRAGMENT_BIT)
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

    // renderPass — from VulkanPipeline::GetRenderPass() (borrowed, not owned)
    bool Initialize(VulkanDevice&                              device,
                    VkRenderPass                               renderPass,
                    const VulkanPipeline::ShaderInfo&          shaders,
                    const VulkanPipeline::VertexInputInfo&     vertexInput);
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
                               const VulkanPipeline::VertexInputInfo&    vertexInput);
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
//  vkCmdBlitImage calls — one blit per mip level — inside a single
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
//    //    (Use vkCreateImage directly — VulkanImage wraps a single level;
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
    // Call before Generate() — if false, fall back to nearest or skip mips.
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
//  EXECUTION & ORCHESTRATION LAYER
//
//  VCK Core gives you the GPU surface.  This layer gives you a frame loop.
//
//  It is entirely opt-in:  if you want to keep driving VulkanSync +
//  VulkanCommand by hand, everything below this point can be ignored — it
//  does not modify any core class, it just composes on top of them.
//
//  PRINCIPLE
//    CPU and GPU are asynchronous producers and consumers.  A frame is a
//    container of work items (CPU jobs + GPU submissions + transient
//    resource scope + dependency tokens), not a render function.  The
//    scheduler decides how far ahead the CPU may run and when the GPU
//    work is actually handed off.
// =============================================================================


// -----------------------------------------------------------------------------
// [13] FramePolicy
//
//   Lockstep    — CPU waits for GPU every frame.  Deterministic.  Slow.
//   Pipelined   — CPU N+1, GPU N.  Standard Vulkan double-buffering.  Default.
//   AsyncMax    — CPU may run up to `maxLag` frames ahead.  Requires
//                 BackpressureGovernor to stay bounded.
// -----------------------------------------------------------------------------
enum class FramePolicy : uint8_t
{
    Lockstep,
    Pipelined,
    AsyncMax,
};

inline const char* FramePolicyName(FramePolicy p)
{
    switch (p)
    {
        case FramePolicy::Lockstep:  return "Lockstep";
        case FramePolicy::Pipelined: return "Pipelined";
        case FramePolicy::AsyncMax:  return "AsyncMax";
    }
    return "?";
}


// -----------------------------------------------------------------------------
// [14] TimelineSemaphore  (opt-in)
//
//  Thin wrapper over VK_KHR_timeline_semaphore.  A single semaphore carries
//  a monotonically increasing 64-bit counter that any producer can signal
//  and any consumer can wait for — no fences, no binary semaphores.
//
//  Requires device creation with VkPhysicalDeviceTimelineSemaphoreFeatures
//  { timelineSemaphore = VK_TRUE }.  VCK's current VulkanDevice does NOT
//  enable that feature, so Initialize() will return false on most setups —
//  callers should be prepared to fall back to VulkanSync's binary fences.
//  Adding the feature bit to the device is a one-line core change planned
//  for a follow-up PR.
// -----------------------------------------------------------------------------
class TimelineSemaphore
{
public:
    TimelineSemaphore()  = default;
    ~TimelineSemaphore() = default;

    TimelineSemaphore(const TimelineSemaphore&)            = delete;
    TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;

    bool Initialize(VulkanDevice& device, uint64_t initialValue = 0);
    void Shutdown();

    VkSemaphore Handle() const { return m_Sem; }
    bool        IsValid() const { return m_Sem != VK_NULL_HANDLE; }

    // Non-blocking read of the current counter.
    uint64_t LastSignaledValue() const;

    // CPU waits for counter >= value (blocks up to timeoutNs).  Returns true on success.
    bool Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) const;

    // Host-side signal (rarely used in GPU pipelines — prefer GPU-side vkQueueSubmit signal).
    bool Signal(uint64_t value);

private:
    VulkanDevice* m_Device = nullptr;
    VkSemaphore   m_Sem    = VK_NULL_HANDLE;
};


// -----------------------------------------------------------------------------
// [15] DependencyToken
//
//  Ordering handle.  Produced by whoever submits GPU work; consumed by
//  whoever needs to wait for it.  Internally: a (TimelineSemaphore*, value)
//  pair.  Invalid tokens are a no-op on wait (decouples hot paths from
//  whether any producer actually ran).
// -----------------------------------------------------------------------------
struct DependencyToken
{
    TimelineSemaphore* sem   = nullptr;
    uint64_t           value = 0;

    bool IsValid() const { return sem != nullptr; }

    // Convenience host wait.  Returns true on success or when invalid.
    bool WaitHost(uint64_t timeoutNs = UINT64_MAX) const
    {
        return !IsValid() || sem->Wait(value, timeoutNs);
    }
};


// -----------------------------------------------------------------------------
// [16] QueueSet
//
//  Holds VkQueue handles for the three logical queue types VCK cares about.
//  Falls back to the graphics queue for any type the current device did not
//  expose a dedicated queue for.  (VCK's current VulkanDevice only creates a
//  graphics queue — so in practice all three slots point at the same queue.
//  The abstraction exists so call sites can be written against multi-queue
//  intent today and pick up real parallelism when VulkanDevice grows
//  dedicated compute / transfer queue support.)
// -----------------------------------------------------------------------------
class QueueSet
{
public:
    bool Initialize(VulkanDevice& device);
    void Shutdown() {}

    VkQueue Graphics() const { return m_Graphics; }
    VkQueue Compute()  const { return m_Compute;  }
    VkQueue Transfer() const { return m_Transfer; }

    uint32_t GraphicsFamily() const { return m_GraphicsFamily; }
    uint32_t ComputeFamily()  const { return m_ComputeFamily;  }
    uint32_t TransferFamily() const { return m_TransferFamily; }

    bool HasDedicatedCompute()  const { return m_Compute  != m_Graphics; }
    bool HasDedicatedTransfer() const { return m_Transfer != m_Graphics; }

private:
    VkQueue  m_Graphics       = VK_NULL_HANDLE;
    VkQueue  m_Compute        = VK_NULL_HANDLE;
    VkQueue  m_Transfer       = VK_NULL_HANDLE;
    uint32_t m_GraphicsFamily = 0;
    uint32_t m_ComputeFamily  = 0;
    uint32_t m_TransferFamily = 0;
};


// -----------------------------------------------------------------------------
// [17] GpuSubmissionBatcher
//
//  Collects per-frame vkQueueSubmit work into one batch per queue and flushes
//  it once at end-of-frame.  Keeps per-draw submission overhead off the hot
//  path.  No reordering is performed here — batches are submitted in the
//  order calls to QueueX() were made.
//
//  Each Queue() call can attach optional (wait, signal) semaphores.  The
//  flush for the Graphics queue accepts the in-flight VkFence that the
//  FrameScheduler will wait on at the start of the next cycle.
// -----------------------------------------------------------------------------
class GpuSubmissionBatcher
{
public:
    struct SubmitInfo
    {
        VkSemaphore          waitSem   = VK_NULL_HANDLE;
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore          signalSem = VK_NULL_HANDLE;

        // Explicit default ctor — MinGW g++ (9.x/10.x) rejects `info = {}`
        // when the nested struct has NSDMI unless the default ctor is
        // explicitly user-declared.  Cheap fix, zero runtime cost.
        SubmitInfo() = default;
    };

    bool Initialize(VulkanDevice& device, QueueSet& queues);
    void Shutdown();

    // Overloads instead of a default-argument — avoids the same MinGW
    // corner case on `SubmitInfo = {}`.
    void QueueGraphics(VkCommandBuffer cmd, const SubmitInfo& info);
    void QueueCompute (VkCommandBuffer cmd, const SubmitInfo& info);
    void QueueTransfer(VkCommandBuffer cmd, const SubmitInfo& info);

    void QueueGraphics(VkCommandBuffer cmd) { QueueGraphics(cmd, SubmitInfo()); }
    void QueueCompute (VkCommandBuffer cmd) { QueueCompute (cmd, SubmitInfo()); }
    void QueueTransfer(VkCommandBuffer cmd) { QueueTransfer(cmd, SubmitInfo()); }

    // Flushes all three queue buckets.  graphicsFence (if not null) is passed
    // to vkQueueSubmit on the graphics queue so the CPU can wait on it.
    void FlushAll(VkFence graphicsFence = VK_NULL_HANDLE);

    // Clear without submitting — used when a swapchain recreate aborts a frame.
    void DiscardAll();

    uint32_t PendingGraphics() const { return static_cast<uint32_t>(m_Graphics.size()); }
    uint32_t PendingCompute()  const { return static_cast<uint32_t>(m_Compute.size());  }
    uint32_t PendingTransfer() const { return static_cast<uint32_t>(m_Transfer.size()); }

private:
    struct Entry { VkCommandBuffer cmd; SubmitInfo info; };

    void FlushQueue(VkQueue q, std::vector<Entry>& bucket, VkFence fence);

    VulkanDevice* m_Device = nullptr;
    QueueSet*     m_Queues = nullptr;

    std::vector<Entry> m_Graphics;
    std::vector<Entry> m_Compute;
    std::vector<Entry> m_Transfer;
};


// -----------------------------------------------------------------------------
// [18] BackpressureGovernor
//
//  Tracks the gap between the CPU frame counter (produced) and the GPU
//  frame counter (retired).  AsyncMaxPolicy uses this to block CPU frame N+k
//  from starting while k > maxLag.  Pipelined / Lockstep do not stall here
//  (Pipelined relies on VulkanSync's per-slot fence; Lockstep waits inline).
// -----------------------------------------------------------------------------
class BackpressureGovernor
{
public:
    void Initialize(FramePolicy policy, uint32_t maxLag);
    void Shutdown();

    void NoteCpuFrameStart(uint64_t absoluteFrame);
    void NoteGpuFrameRetired(uint64_t absoluteFrame);

    // Non-blocking overrun check (AsyncMax only).  FrameScheduler uses this
    // to decide whether to force an extra fence wait.  The governor itself
    // never blocks — earlier versions waited on a condition variable, but
    // NoteGpuFrameRetired is always called on the same render thread, which
    // self-deadlocked the CV path the moment the CPU overran.
    bool     IsOverrun() const;
    uint64_t WaitIfOverrun();   // kept for API compat — always returns 0

    FramePolicy Policy() const { return m_Policy; }
    uint32_t    MaxLag() const { return m_MaxLag; }
    uint64_t    CpuFrame() const { return m_CpuFrame.load(); }
    uint64_t    GpuFrame() const { return m_GpuFrame.load(); }
    uint32_t    Lag()      const
    {
        const uint64_t c = m_CpuFrame.load();
        const uint64_t g = m_GpuFrame.load();
        return c > g ? static_cast<uint32_t>(c - g) : 0u;
    }

private:
    FramePolicy m_Policy = FramePolicy::Pipelined;
    uint32_t    m_MaxLag = 2;

    std::atomic<uint64_t> m_CpuFrame{0};
    std::atomic<uint64_t> m_GpuFrame{0};

    std::mutex              m_Mu;
    std::condition_variable m_Cv;
};


// -----------------------------------------------------------------------------
// [19] JobGraph
//
//  Minimal CPU job / task graph.  Designed to be frame-scoped: Reset()
//  between frames; Add() during the frame; Execute() runs everything on the
//  internal worker pool respecting declared deps.
//
//  This is NOT a production-grade scheduler — no fibers, no work stealing,
//  no priority.  It's a correct baseline:  std::thread workers + mutex +
//  condition_variable + atomic per-job pending-deps counter.  Good enough
//  to demonstrate the frame / jobs / GPU-submit pipeline; replace with
//  something fancier later without changing the surface.
// -----------------------------------------------------------------------------
class JobGraph
{
public:
    using JobId = uint32_t;
    using Fn    = std::function<void()>;

    JobGraph()  = default;
    ~JobGraph() { Shutdown(); }

    JobGraph(const JobGraph&)            = delete;
    JobGraph& operator=(const JobGraph&) = delete;

    // workerCount = 0 → std::thread::hardware_concurrency(), clamped to [1,32].
    void Initialize(uint32_t workerCount = 0);
    void Shutdown();

    // Register a new job.  deps are job IDs returned from earlier Add() calls.
    // Safe to call only between Reset() and Execute().
    JobId Add(const char* name, Fn fn, std::initializer_list<JobId> deps = {});

    // Kick the graph and block until every job has run.
    void  Execute();

    // Drop all pending jobs — typically called from FrameScheduler::BeginFrame.
    void  Reset();

    uint32_t JobCount()    const { return static_cast<uint32_t>(m_Jobs.size()); }
    uint32_t WorkerCount() const { return static_cast<uint32_t>(m_Workers.size()); }

private:
    struct Job
    {
        JobId                    id = 0;
        const char*              name = "";
        Fn                       fn;
        std::vector<JobId>       dependents;
        std::atomic<uint32_t>    pendingDeps{0};
        std::atomic<bool>        done{false};
    };

    void WorkerLoop();
    void EnqueueReady(JobId id);
    void FinishJob(JobId id);

    std::vector<std::unique_ptr<Job>>  m_Jobs;
    std::vector<JobId>                 m_Ready;
    std::mutex                         m_Mu;
    std::condition_variable            m_CvWork;
    std::condition_variable            m_CvDone;

    std::vector<std::thread>           m_Workers;
    std::atomic<uint32_t>              m_Outstanding{0};
    std::atomic<bool>                  m_Exit{false};
    bool                               m_Executing = false;
};


// -----------------------------------------------------------------------------
// [20] DebugTimeline
//
//  Plain-text span recorder.  Call BeginCpuSpan / EndCpuSpan around chunks of
//  CPU work; NoteStall() when you discover the CPU was forced to wait; call
//  RecordGpuSpan() after reading GPU timestamps back.  Dump() prints a
//  chronological view to LogVk().  Enable per FrameScheduler config — when
//  disabled, every method is a cheap no-op.
// -----------------------------------------------------------------------------
class DebugTimeline
{
public:
    struct Span
    {
        std::string name;
        const char* track;      // "CPU" | "GPU" | "STALL"
        uint64_t    frame;
        uint64_t    startUs;
        uint64_t    endUs;      // 0 for point events (STALL)
    };

    void Initialize(bool enabled);
    void Shutdown();

    bool Enabled() const { return m_Enabled; }

    void BeginCpuSpan(const char* name, uint64_t frame);
    void EndCpuSpan  (const char* name, uint64_t frame);
    void RecordGpuSpan(const char* name, uint64_t frame,
                       uint64_t startUs, uint64_t endUs);
    void NoteStall(const char* reason, uint64_t frame, uint64_t durationUs);

    // Print accumulated spans to LogVk() and forget them.
    void Dump();
    // Keep buffer, drop spans (call between frames to avoid unbounded growth).
    void ResetBuffer();

private:
    uint64_t NowUs() const;

    bool                                 m_Enabled = false;
    std::chrono::steady_clock::time_point m_Origin;
    std::mutex                           m_Mu;
    std::vector<Span>                    m_Spans;

    // Open CPU spans, keyed by name for simple push/pop semantics.
    std::vector<Span>                    m_OpenCpu;
};


// -----------------------------------------------------------------------------
// [21] Frame
//
//  Lightweight handle exposed to user code during BeginFrame .. EndFrame.
//  Not constructed by the user — owned by the FrameScheduler.  Provides
//  typed access to the slot's per-frame primitives plus the per-frame
//  batcher and job graph.
// -----------------------------------------------------------------------------
class FrameScheduler;   // fwd — friendship below

class Frame
{
public:
    uint32_t    Slot()      const { return m_Slot;     }
    uint64_t    Absolute()  const { return m_Absolute; }
    FramePolicy Policy()    const { return m_Policy;   }

    VkFence         Fence()           const { return m_Fence;           }
    VkSemaphore     ImageAvailable()  const { return m_ImageAvailable;  }
    VkSemaphore     RenderFinished()  const { return m_RenderFinished;  }
    VkCommandBuffer PrimaryCmd()      const { return m_PrimaryCmd;      }

    GpuSubmissionBatcher& Submissions() { return *m_Submissions; }
    JobGraph&             Jobs()        { return *m_Jobs;        }

    //  Convenience: queue PrimaryCmd onto the graphics bucket of the per-frame
    //  GpuSubmissionBatcher.  Equivalent to:
    //      f.Submissions().QueueGraphics(f.PrimaryCmd(), info);
    //  The no-arg overload wires the frame's own ImageAvailable / RenderFinished
    //  semaphores — the common case for a simple render-then-present loop.
    void QueueGraphics()
    {
        GpuSubmissionBatcher::SubmitInfo info;
        info.waitSem   = m_ImageAvailable;
        info.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        info.signalSem = m_RenderFinished;
        m_Submissions->QueueGraphics(m_PrimaryCmd, info);
    }

    void QueueGraphics(const GpuSubmissionBatcher::SubmitInfo& info)
    {
        m_Submissions->QueueGraphics(m_PrimaryCmd, info);
    }

private:
    friend class FrameScheduler;

    uint32_t    m_Slot     = 0;
    uint64_t    m_Absolute = 0;
    FramePolicy m_Policy   = FramePolicy::Pipelined;

    VkFence         m_Fence           = VK_NULL_HANDLE;
    VkSemaphore     m_ImageAvailable  = VK_NULL_HANDLE;
    VkSemaphore     m_RenderFinished  = VK_NULL_HANDLE;
    VkCommandBuffer m_PrimaryCmd      = VK_NULL_HANDLE;

    GpuSubmissionBatcher* m_Submissions = nullptr;
    JobGraph*             m_Jobs        = nullptr;
};


// -----------------------------------------------------------------------------
// [22] FrameScheduler
//
//  Top-level execution orchestrator.
//
//    Initialize(device, command, sync, cfg)
//        Wires everything up.  Requires the core objects already created.
//
//    Frame& f = BeginFrame();
//        1. Advances absolute frame counter.
//        2. Applies FramePolicy — may block on the slot's in-flight fence
//           (Lockstep / Pipelined) or defer blocking to BackpressureGovernor
//           (AsyncMax).
//        3. Resets the slot's JobGraph + batcher.
//        4. Resets command buffer via VulkanCommand::BeginRecording().
//
//    f.Jobs().Add(...);   // register CPU work with deps
//    DispatchJobs();      // runs the graph, blocks until all CPU jobs done
//
//    // Record vkCmd* into f.PrimaryCmd().  Push batched submits via
//    // f.Submissions().QueueGraphics(cmd, ...).
//
//    EndFrame();
//        1. Flushes GpuSubmissionBatcher.
//        2. Hands the frame's fence to the graphics queue so GPU retirement
//           can be tracked.
//        3. Advances VulkanSync's frame index.
//
//  The scheduler NEVER calls vkAcquireNextImageKHR or vkQueuePresentKHR.
//  Those remain the caller's responsibility; the scheduler just owns timing.
// -----------------------------------------------------------------------------
class FrameScheduler
{
public:
    struct Config
    {
        FramePolicy policy         = FramePolicy::Pipelined;
        uint32_t    asyncMaxLag    = 2;    // only relevant for AsyncMax
        bool        enableTimeline = false;
        uint32_t    jobWorkers     = 0;    // 0 → hardware_concurrency clamped

        // See SubmitInfo above — explicit default ctor works around a
        // MinGW g++ bug with NSDMI + `cfg = {}` default arguments.
        Config() = default;
    };

    FrameScheduler()  = default;
    ~FrameScheduler() = default;

    FrameScheduler(const FrameScheduler&)            = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    bool Initialize(VulkanDevice&  device,
                    VulkanCommand& command,
                    VulkanSync&    sync,
                    Config         cfg);

    // No-config overload — defaults to `Config()`.
    bool Initialize(VulkanDevice&  device,
                    VulkanCommand& command,
                    VulkanSync&    sync)
    {
        return Initialize(device, command, sync, Config());
    }
    void Shutdown();

    // Frame lifecycle — must be called in order, once per frame.
    Frame& BeginFrame();
    void   DispatchJobs();
    void   EndFrame();

    // Accessors.
    uint64_t              AbsoluteFrame()      const { return m_Absolute; }
    uint32_t              CurrentSlot()        const;
    FramePolicy           Policy()             const { return m_Cfg.policy; }
    uint64_t              LastRetiredFrame()   const { return m_Governor.GpuFrame(); }
    bool                  InFrame()            const { return m_InFrame; }

    QueueSet&             Queues()       { return m_Queues;      }
    GpuSubmissionBatcher& Submissions()  { return m_Submissions; }
    BackpressureGovernor& Governor()     { return m_Governor;    }
    DebugTimeline&        Timeline()     { return m_Timeline;    }

    const Config&         Cfg()          const { return m_Cfg; }

private:
    void WaitInFlightFence(uint32_t slot);
    void RetireCompletedFrames();

    VulkanDevice*  m_Device  = nullptr;
    VulkanCommand* m_Command = nullptr;
    VulkanSync*    m_Sync    = nullptr;

    Config m_Cfg;

    QueueSet             m_Queues;
    GpuSubmissionBatcher m_Submissions;
    BackpressureGovernor m_Governor;
    DebugTimeline        m_Timeline;

    std::array<Frame,    MAX_FRAMES_IN_FLIGHT> m_Frames{};
    std::array<JobGraph, MAX_FRAMES_IN_FLIGHT> m_Jobs{};
    std::array<uint64_t, MAX_FRAMES_IN_FLIGHT> m_SlotAbsolute{};

    uint64_t m_Absolute = 0;
    bool     m_InFrame  = false;
};


} // namespace VCK
