// =============================================================================
//  VCK.h  -  Vulkan Core Kit
//
//  Single-header amalgam of the VCK core Vulkan layer.  Include this file
//  and you get every core class, struct, macro, and constant in one shot.
//  VCKExpansion.h is auto-included at the bottom, so this one header is
//  enough to use the entire kit.
//
//  LAYERING
//  ────────
//      VCK core      (this file)           - instance, device, swapchain, ...
//          ↓
//      VCK expansion (VCKExpansion.h)      - textures, meshes, descriptors, ...
//          ↓
//      VCK memory    (VMM/VulkanMemoryManager.h, optional)
//          ↓
//      Your renderer / game / tool
//
//  SOURCE FILES ASSEMBLED (core)
//  ─────────────────────────────
//  Headers   (.h)  : VulkanHelpers   VulkanContext   VulkanDevice
//                    VulkanSwapchain  VulkanBuffer   VulkanImage
//                    VulkanPipeline   VulkanCommand  VulkanSync
//
//  Implementations (.cpp) - function index at the bottom of this file:
//                    VmaImpl         VulkanContext   VulkanDevice
//                    VulkanSwapchain VulkanBuffer    VulkanImage
//                    VulkanPipeline  VulkanCommand   VulkanSync
//                    VulkanHelpers
//
//  INIT / SHUTDOWN ORDER
//  ─────────────────────
//  Init:      Context → Device → Swapchain → Pipeline → Command → Sync
//  Shutdown:  Sync → Command → Pipeline → Swapchain → Device → Context
//
//  Expansion objects (VCKExpansion.h) and VMM resources must be shut down
//  BEFORE the core objects they reference.
//
//  ONE-HOUR QUICK START
//  ────────────────────
//    1.  Create a window (GLFW / Win32) and grab its HWND.
//    2.  Build a VCK::Config if you want non-default knobs (optional):
//            VCK::Config cfg;
//            cfg.swapchain.presentMode = VCK::PresentMode::Mailbox;
//            cfg.sync.framesInFlight   = 3;
//    3.  Run the init chain with either zero-arg or Config overloads:
//            ctx .Initialize(hwnd, cfg);
//            dev .Initialize(ctx,  cfg);
//            sc  .Initialize(dev, ctx, w, h, cfg);
//            pipe.Initialize(dev, sc,  shaders, vertexInput);
//            cmd .Initialize(dev,  cfg);
//            sync.Initialize(dev,  cfg);
//    4.  Drive the frame loop yourself (see RGBTriangle) OR hand off to
//        VCK::FrameScheduler (see HelloExample).
//    5.  Shut down in reverse order.
//
//  Every Initialize(...) has a zero-arg form - if you pass no Config you get
//  exactly the same behaviour as before Config existed.  The library never
//  owns things it did not construct, never hides Vk handles, and every
//  "preferred" overload is implemented as a one-line forward to the raw-handle
//  form - so you can always drop down to manual Vulkan for anything VCK is
//  not doing for you.
//
//  NAMESPACE
//  ─────────
//  Everything (core + expansion) lives in:  namespace VCK { ... }
//  LogVk / VK_CHECK are deliberately at global scope so every TU can use
//  them without a `using` declaration.
// =============================================================================

#pragma once

// ─── System / Vulkan prerequisites ───────────────────────────────────────────
// Platform detection lives in VCKCrossplatform.h - include it first so
// the VCK_PLATFORM_* macros are defined before we use them below.
#include "VCKCrossplatform.h"

#if VCK_PLATFORM_WINDOWS
    // WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake / build.bat -
    // do NOT redefine here.
    #include <windows.h>
#endif

#include <vulkan/vulkan.h>

#if VCK_PLATFORM_WINDOWS
    #include <vulkan/vulkan_win32.h>
#endif

#include "vk_mem_alloc.h"
#include "core/VulkanHelpers.h"   // LogVk, VK_CHECK, VCK::Config, PresentMode, QueuePreference, MAX_FRAMES_IN_FLIGHT

#include <cstdio>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <cstdint>


// =============================================================================
//  ██╗  ██╗███████╗██╗     ██████╗ ███████╗██████╗ ███████╗
//  ██║  ██║██╔════╝██║     ██╔══██╗██╔════╝██╔══██╗██╔════╝
//  ███████║█████╗  ██║     ██████╔╝█████╗  ██████╔╝███████╗
//  ██╔══██║██╔══╝  ██║     ██╔═══╝ ██╔══╝  ██╔══██╗╚════██║
//  ██║  ██║███████╗███████╗██║     ███████╗██║  ██║███████║
//  ╚═╝  ╚═╝╚══════╝╚══════╝╚═╝     ╚══════╝╚═╝  ╚═╝╚══════╝
// =============================================================================

// -----------------------------------------------------------------------------
//  VulkanHelpers.h
//  Global logging utility and VK_CHECK macro.
//  No namespace - usable everywhere.
// -----------------------------------------------------------------------------

// LogVk and VK_CHECK are provided by VulkanHelpers.h (included above).  They
// remain at global scope so every TU can use them without a `using` decl.


// =============================================================================
//  ██████╗ ██╗      █████╗ ███████╗███████╗███████╗███████╗
//  ██╔════╝██║     ██╔══██╗██╔════╝██╔════╝██╔════╝██╔════╝
//  ██║     ██║     ███████║███████╗███████╗█████╗  ███████╗
//  ██║     ██║     ██╔══██║╚════██║╚════██║██╔══╝  ╚════██║
//  ╚██████╗███████╗██║  ██║███████║███████║███████╗███████║
//   ╚═════╝╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝╚══════╝
// =============================================================================

// The class declarations below were previously inlined in this file; they now
// live in the per-class headers so there is a single source of truth.  Users
// still include only "VCK.h" (or "VCKExpansion.h") - the amalgam pulls in the
// whole core API.  Internal .cpp files can keep including individual headers.

#include "core/VulkanContext.h"
#include "core/VulkanDevice.h"
#include "core/VulkanSwapchain.h"
#include "core/VulkanBuffer.h"
#include "core/VulkanImage.h"
#include "core/VulkanPipeline.h"
#include "core/VulkanSync.h"
#include "core/VulkanCommand.h"
#include "VCKCrossplatform.h"   // VCK::Window + platform detection (Windows / Linux / macOS)




// =============================================================================
//  ███████╗██╗  ██╗██████╗  █████╗ ███╗   ██╗███████╗██╗ ██████╗ ███╗   ██╗
//  ██╔════╝╚██╗██╔╝██╔══██╗██╔══██╗████╗  ██║██╔════╝██║██╔═══██╗████╗  ██║
//  █████╗   ╚███╔╝ ██████╔╝███████║██╔██╗ ██║███████╗██║██║   ██║██╔██╗ ██║
//  ██╔══╝   ██╔██╗ ██╔═══╝ ██╔══██║██║╚██╗██║╚════██║██║██║   ██║██║╚██╗██║
//  ███████╗██╔╝ ██╗██║     ██║  ██║██║ ╚████║███████║██║╚██████╔╝██║ ╚████║
//  ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝
// =============================================================================
//  VCKExpansion  -  higher-level building blocks on top of VCK core
//
//  Declared in VCKExpansion.h (auto-included below).
//  Implemented in VCKExpansion.cpp.
//  All classes live in namespace VCK.
//
//  DESIGN RULE
//  ───────────
//  Every expansion class receives the core objects by reference or raw
//  pointer.  They do NOT own, construct, or destroy any core object.
//  The core init/shutdown order is completely unchanged:
//
//    Init:      Context → Device → Swapchain → Pipeline → Command → Sync
//    Shutdown:  Sync → Command → Pipeline → Swapchain → Device → Context
//
//  All expansion objects must be Shutdown() BEFORE the core objects they
//  reference.  A typical ordering is shown in the function index below.
//
//  SOURCE FILES ADDED
//  ──────────────────
//  Header         : VCKExpansion.h
//  Implementation : VCKExpansion.cpp
//
//  CLASSES  (12)
//  ─────────────
//  [1]  VulkanOneTimeCommand          - one-shot GPU command using the existing pool
//  [2]  VulkanFramebufferSet          - per-swapchain-image VkFramebuffers
//  [3]  VulkanDepthBuffer             - depth/stencil image wrapping VulkanImage
//  [4]  VulkanSampler                 - VkSampler with nearest / linear factory methods
//  [5]  VulkanTexture                 - VulkanImage + VulkanSampler, hidden staging upload
//  [6]  VulkanMesh                    - vertex + index VulkanBuffers with RecordDraw()
//  [7]  VulkanDescriptorLayoutBuilder - fluent VkDescriptorSetLayout builder
//  [8]  VulkanDescriptorPool          - VkDescriptorPool + per-frame set allocation
//  [9]  VulkanUniformSet<T>           - per-frame typed UBO with Write() + GetSet()
//  [10] VulkanDescriptorAllocator     - general-purpose pool supporting multiple descriptor types
//  [11] VulkanModelPipeline           - full model pipeline with UBO layouts + push constants
//  [12] VulkanMipmapGenerator         - blit-based mip chain generation for any VkImage
//
//  HELPER FUNCTION (file-static, internal)
//  ───────────────────────────────────────
//  FindDepthFormat(VulkanDevice&)
//    Iterates D32_SFLOAT → D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT and
//    returns the first format that supports DEPTH_STENCIL_ATTACHMENT optimal
//    tiling.  Called internally by VulkanDepthBuffer::Initialize() and Recreate().
// =============================================================================


// -----------------------------------------------------------------------------
//  VulkanOneTimeCommand
//
//  Allocates a single transient VkCommandBuffer from the existing
//  VulkanCommand pool, records your work, submits it to the graphics queue,
//  and blocks (vkQueueWaitIdle) until the GPU is done.
//  Frees the command buffer on End().
//
//  Use for one-off GPU operations: staging copies, image layout transitions,
//  mipmap generation.  Not intended for per-frame use.
//
//  Usage:
//    VulkanOneTimeCommand otc;
//    if (otc.Begin(device, command))
//    {
//        // record vkCmd* calls via otc.Cmd()
//        otc.End();   // submit + vkQueueWaitIdle + free
//    }
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool            Begin(VulkanDevice& device, VulkanCommand& command)
//    Allocates from command.GetCommandPool() and opens the buffer.
//    Returns false if allocation or vkBeginCommandBuffer fails.
//  void            End()
//    Ends recording, submits to device.GetGraphicsQueue(),
//    calls vkQueueWaitIdle, then frees the buffer back to the pool.
//
//  ── Accessor ──────────────────────────────────────────────────────────────
//  VkCommandBuffer Cmd() const
//    Returns the open command buffer.  Valid between Begin() and End().
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanFramebufferSet
//
//  Creates one VkFramebuffer per swapchain image and stores them in a vector.
//  Optionally attaches a depth image view as the second framebuffer attachment.
//
//  Typically created after VulkanPipeline and recreated on every resize.
//  Pass Get(imageIndex) as the framebuffer in VkRenderPassBeginInfo.
//
//  Usage:
//    VulkanFramebufferSet fbs;
//    fbs.Initialize(device, swapchain, pipeline.GetRenderPass(),
//                   depth.GetImageView());   // omit last arg for colour-only
//
//    // on resize (after swapchain.Recreate() + depth.Recreate()):
//    fbs.Recreate(pipeline.GetRenderPass(), depth.GetImageView());
//
//    // per frame:
//    beginInfo.framebuffer = fbs.Get(frame.ImageIndex);
//
//    fbs.Shutdown();   // before pipeline.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&    device,
//                  VulkanSwapchain& swapchain,
//                  VkRenderPass     renderPass,
//                  VkImageView      depthView = VK_NULL_HANDLE)
//    Creates one framebuffer per swapchain image view.
//    Pass VK_NULL_HANDLE for depthView on colour-only render passes.
//  void Shutdown()
//    Destroys all framebuffers and releases references.
//  bool Recreate(VkRenderPass renderPass,
//                VkImageView  depthView = VK_NULL_HANDLE)
//    Destroys existing framebuffers and rebuilds from the current swapchain
//    image views.  Call after VulkanSwapchain::Recreate().
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkFramebuffer Get(uint32_t imageIndex) const
//    Returns the framebuffer for the given swapchain image index.
//  uint32_t      Count() const
//    Number of framebuffers (equals swapchain image count).
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDepthBuffer
//
//  Depth/stencil image backed by VulkanImage.  The best available format is
//  chosen automatically by FindDepthFormat():
//    D32_SFLOAT  →  D32_SFLOAT_S8_UINT  →  D24_UNORM_S8_UINT
//
//  The image is created and immediately transitioned to
//  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL on Initialize().
//
//  Usage:
//    VulkanDepthBuffer depth;
//    depth.Initialize(device, width, height);
//
//    // pass to VulkanFramebufferSet:
//    fbs.Initialize(device, swapchain, renderPass, depth.GetImageView());
//
//    // on resize (after swapchain.Recreate()):
//    depth.Recreate(newWidth, newHeight);
//
//    depth.Shutdown();   // before device.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice& device, uint32_t width, uint32_t height)
//  void Shutdown()
//  bool Recreate(uint32_t width, uint32_t height)
//    Calls Shutdown() + Initialize() in one step.  device reference is kept
//    from the original Initialize() call.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkImageView GetImageView() const
//  VkFormat    GetFormat()    const
//  bool        IsValid()      const
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanSampler
//
//  Thin wrapper around VkSampler with two filter-mode factory methods.
//  Anisotropy is queried from the physical device and enabled automatically
//  when the device supports it.
//
//  Usage:
//    VulkanSampler sampler;
//    sampler.CreateLinear(device);
//
//    // in a descriptor write:
//    imageInfo.sampler = sampler.GetSampler();
//
//    sampler.Shutdown();   // before device.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool CreateNearest(VulkanDevice& device)
//    VK_FILTER_NEAREST, no mipmapping.
//    Best for pixel-perfect / UI textures.
//  bool CreateLinear(VulkanDevice& device)
//    VK_FILTER_LINEAR, no mipmapping.
//    General-purpose smooth textures.
//  void Shutdown()
//    Destroys the VkSampler.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkSampler GetSampler() const
//  bool      IsValid()    const
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanTexture
//
//  One-stop GPU texture: VulkanImage (R8G8B8A8_SRGB) + VulkanSampler (linear).
//  CreateFromPixels() performs the complete CPU→GPU upload without any manual
//  staging management:
//    1. Allocates a staging VulkanBuffer.
//    2. Copies pixels into it.
//    3. Opens a VulkanOneTimeCommand.
//    4. Transitions image UNDEFINED → TRANSFER_DST_OPTIMAL.
//    5. Copies buffer → image.
//    6. Transitions image TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
//    7. Submits, waits, frees the staging buffer.
//
//  Usage:
//    VulkanTexture tex;
//    tex.CreateFromPixels(device, command, pixels, 512, 512);
//
//    // in a descriptor write:
//    imageInfo.imageView   = tex.GetImageView();
//    imageInfo.sampler     = tex.GetSampler();
//    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//
//    tex.Shutdown();   // before device.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool CreateFromPixels(VulkanDevice&  device,
//                        VulkanCommand& command,
//                        const uint8_t* pixels,
//                        uint32_t       width,
//                        uint32_t       height)
//    pixels must point to width * height * 4 bytes (RGBA8).
//  void Shutdown()
//    Destroys the image and sampler.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkImageView GetImageView() const
//  VkSampler   GetSampler()   const
//  bool        IsValid()      const
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanMesh
//
//  GPU-resident vertex buffer and optional index buffer.
//  Upload is done once via a VulkanOneTimeCommand + staging buffers.
//  Staging memory is freed immediately after the GPU copy completes.
//  Call RecordDraw() every frame inside an active render pass.
//
//  Usage:
//    VulkanMesh mesh;
//    mesh.Upload(device, command,
//                verts.data(), sizeof(verts),
//                indices.data(), (uint32_t)indices.size());
//
//    // non-indexed:
//    mesh.Upload(device, command, verts.data(), sizeof(verts), nullptr, 0);
//
//    // inside render pass, per frame:
//    mesh.RecordDraw(cmd);
//
//    mesh.Shutdown();   // before device.Shutdown()
//
//  RecordDraw() will:
//    • vkCmdBindVertexBuffers  - binding 0
//    • vkCmdBindIndexBuffer    - only when an index buffer was uploaded
//    • vkCmdDrawIndexed / vkCmdDraw
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Upload(VulkanDevice&   device,
//              VulkanCommand&  command,
//              const void*     vertices,    // raw vertex data
//              VkDeviceSize    vertexSize,  // total byte size of vertex data
//              const uint32_t* indices,     // uint32 index array (or nullptr)
//              uint32_t        indexCount)  // 0 for non-indexed
//  void Shutdown()
//
//  ── Per-frame recording ───────────────────────────────────────────────────
//  void RecordDraw(VkCommandBuffer cmd) const
//    Binds buffers and issues one draw call.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  bool IsValid()    const   - true if the vertex buffer was uploaded successfully
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDescriptorLayoutBuilder
//
//  Fluent builder for VkDescriptorSetLayout.
//  Chain Add() calls in binding-slot order, then call Build().
//  The returned layout is caller-owned - destroy it with
//  vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr) when done.
//
//  Usage:
//    VkDescriptorSetLayout layout =
//        VulkanDescriptorLayoutBuilder{}
//            .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                    VK_SHADER_STAGE_VERTEX_BIT)
//            .Add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//                    VK_SHADER_STAGE_FRAGMENT_BIT)
//            .Build(device);
//
//    // …use layout…
//
//    vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr);
//
//  ── Builder methods ───────────────────────────────────────────────────────
//  VulkanDescriptorLayoutBuilder& Add(uint32_t           binding,
//                                     VkDescriptorType   type,
//                                     VkShaderStageFlags stages)
//    Appends one binding and returns *this for chaining.
//  VkDescriptorSetLayout Build(VulkanDevice& device) const
//    Creates and returns the layout.  Returns VK_NULL_HANDLE on failure.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDescriptorPool
//
//  Creates a VkDescriptorPool pre-sized for one descriptor type and
//  pre-allocates exactly MAX_FRAMES_IN_FLIGHT (2) descriptor sets from a
//  given layout in a single Initialize() call - the standard pattern for
//  per-frame uniform data.
//
//  Usage:
//    VkDescriptorSetLayout layout =
//        VulkanDescriptorLayoutBuilder{}
//            .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                    VK_SHADER_STAGE_VERTEX_BIT)
//            .Build(device);
//
//    VulkanDescriptorPool pool;
//    pool.Initialize(device, layout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
//
//    // per frame:
//    VkDescriptorSet set = pool.GetSet(frameIndex);
//
//    pool.Shutdown();
//    vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr);
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&         device,
//                  VkDescriptorSetLayout layout,
//                  VkDescriptorType      type)
//    Creates the pool and allocates MAX_FRAMES_IN_FLIGHT sets from layout.
//  void Shutdown()
//    Destroys the pool (all allocated sets are implicitly freed).
//
//  ── Accessor ──────────────────────────────────────────────────────────────
//  VkDescriptorSet GetSet(uint32_t frameIndex) const
//    Returns the pre-allocated descriptor set for the given frame slot.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanUniformSet<T>
//
//  Template wrapper: one per-frame uniform buffer of type T, wired to the
//  descriptor sets provided by VulkanDescriptorPool.
//  T must be a trivially-copyable struct (matrices, light params, etc.).
//
//  Initialize() creates MAX_FRAMES_IN_FLIGHT (2) VulkanBuffer::CreateUniform
//  buffers and calls vkUpdateDescriptorSets to point each frame's descriptor
//  set at its matching buffer.
//  Write() does a plain memcpy into the persistently-mapped buffer.
//
//  Usage:
//    struct FrameUBO { float viewProj[16]; float camPos[4]; };
//
//    VkDescriptorSetLayout layout =
//        VulkanDescriptorLayoutBuilder{}
//            .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                    VK_SHADER_STAGE_VERTEX_BIT)
//            .Build(device);
//
//    VulkanDescriptorPool pool;
//    pool.Initialize(device, layout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
//
//    VulkanUniformSet<FrameUBO> ubo;
//    ubo.Initialize(device, pool, 0 /* binding */);
//
//    // every frame:
//    ubo.Write(frameIndex, frameData);
//    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
//                            pipelineLayout, 0, 1,
//                            &ubo.GetSet(frameIndex), 0, nullptr);
//
//    ubo.Shutdown();   // before pool.Shutdown()
//    pool.Shutdown();
//    vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr);
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&        device,
//                  VulkanDescriptorPool& pool,
//                  uint32_t              binding)
//    Allocates per-frame UBO buffers and writes descriptor updates.
//  void Shutdown()
//    Calls VulkanBuffer::Shutdown() on all per-frame buffers.
//
//  ── Per-frame use ─────────────────────────────────────────────────────────
//  void            Write(uint32_t frameIndex, const T& data)
//    memcpy T into the frame slot's mapped uniform buffer.
//  VkDescriptorSet GetSet(uint32_t frameIndex) const
//    Returns the descriptor set for binding via vkCmdBindDescriptorSets.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDescriptorAllocator
//
//  General-purpose VkDescriptorPool that supports multiple descriptor types in
//  a single pool and exposes Allocate() to pull individual VkDescriptorSets
//  from any compatible layout.  All sets are freed implicitly when Shutdown()
//  destroys the pool.
//
//  Unlike VulkanDescriptorPool (which pre-allocates a fixed number of sets of
//  one type), this class is suitable for mixed layouts - e.g. a set-0 UBO
//  layout and a set-1 sampler layout allocated from the same pool.
//
//  Usage:
//    VulkanDescriptorAllocator alloc;
//    alloc.Initialize(device, 8,
//        {{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         4 },
//         { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }});
//
//    VkDescriptorSet s0 = alloc.Allocate(set0Layout);
//    VkDescriptorSet s1 = alloc.Allocate(set1Layout);
//    // ...use sets...
//    alloc.Shutdown();   // frees pool + all sets implicitly
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&                   device,
//                  uint32_t                        maxSets,
//                  initializer_list<PoolSize>      sizes)
//    Creates the pool.  maxSets is the total set count across all Allocate()
//    calls.  sizes lists each descriptor type and its total count in the pool.
//  void Shutdown()
//    Destroys the pool (all allocated sets freed implicitly).
//
//  ── Allocation ────────────────────────────────────────────────────────────
//  VkDescriptorSet Allocate(VkDescriptorSetLayout layout)
//    Allocates one descriptor set.  Returns VK_NULL_HANDLE on failure.
//
//  ── Nested type ───────────────────────────────────────────────────────────
//  struct PoolSize { VkDescriptorType type; uint32_t count; }
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanModelPipeline
//
//  A fully-wired model pipeline.  VulkanPipeline owns the VkRenderPass and
//  creates a baseline VkPipeline with an empty layout.  VulkanModelPipeline
//  takes that render pass and builds the properly-wired VkPipeline that App
//  actually draws with.
//
//  Descriptor layout:
//    set 0, binding 0 - per-frame UBO          (VK_SHADER_STAGE_VERTEX_BIT)
//    set 1, binding 0 - combined image/sampler (VK_SHADER_STAGE_FRAGMENT_BIT)
//
//  Push constant (VK_SHADER_STAGE_VERTEX_BIT, 64 bytes):
//    mat4 model
//
//  Usage (after VulkanPipeline::Initialize has run):
//    VulkanModelPipeline mp;
//    mp.Initialize(device, pipeline.GetRenderPass(), shaders, vertexInput);
//
//    // per frame:
//    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mp.GetPipeline());
//    vkCmdBindDescriptorSets(cmd, ..., mp.GetPipelineLayout(), ...);
//    vkCmdPushConstants(cmd, mp.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT,
//                       0, 64, &pc);
//    mesh.RecordDraw(cmd);
//
//    // shutdown before VulkanPipeline:
//    mp.Shutdown();
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&                          device,
//                  VkRenderPass                           renderPass,
//                  const VulkanPipeline::ShaderInfo&      shaders,
//                  const VulkanPipeline::VertexInputInfo& vertexInput)
//    renderPass is borrowed from VulkanPipeline::GetRenderPass() - NOT owned.
//  void Shutdown()
//    Destroys pipeline, pipeline layout, and both descriptor set layouts.
//    Must be called before the VulkanPipeline that provided the render pass.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkPipeline            GetPipeline()       const
//  VkPipelineLayout      GetPipelineLayout() const
//  VkDescriptorSetLayout GetSet0Layout()     const   - use with VulkanDescriptorAllocator
//  VkDescriptorSetLayout GetSet1Layout()     const   - use with VulkanDescriptorAllocator
// -----------------------------------------------------------------------------


// =============================================================================
//  IMPLEMENTATION FUNCTION INDEX
//  All functions defined across the .cpp files, grouped by translation unit.
//  (Declarations above; implementations in their respective .cpp files.)
// =============================================================================

/*
────────────────────────────────────────────────────────────────────────────────
 VmaImpl.cpp
   (VMA implementation unit - define VMA_IMPLEMENTATION before including
    vk_mem_alloc.h in exactly one .cpp file)
────────────────────────────────────────────────────────────────────────────────

────────────────────────────────────────────────────────────────────────────────
 VulkanHelpers.cpp
   (currently empty - LogVk and VK_CHECK are header-inline)
────────────────────────────────────────────────────────────────────────────────

────────────────────────────────────────────────────────────────────────────────
 VulkanContext.cpp
────────────────────────────────────────────────────────────────────────────────
 bool        VulkanContext::Initialize(HWND windowHandle, const std::string& appName)
 void        VulkanContext::Shutdown()
 bool        VulkanContext::CreateInstance(const std::string& appName)
 bool        VulkanContext::CreateDebugMessenger()
 bool        VulkanContext::CreateSurface(HWND windowHandle)
 bool        VulkanContext::CheckValidationLayerSupport()
 vector<const char*> VulkanContext::BuildRequiredExtensions()
 VkBool32    VulkanContext::DebugCallback(severity, type, pCallbackData, pUserData)

────────────────────────────────────────────────────────────────────────────────
 VulkanDevice.cpp
────────────────────────────────────────────────────────────────────────────────
 bool                VulkanDevice::Initialize(VkInstance, VkSurfaceKHR)
 void                VulkanDevice::Shutdown()
 bool                VulkanDevice::PickPhysicalDevice(VkInstance, VkSurfaceKHR)
 int                 VulkanDevice::ScorePhysicalDevice(VkPhysicalDevice, VkSurfaceKHR) const
 bool                VulkanDevice::IsPhysicalDeviceSuitable(VkPhysicalDevice, VkSurfaceKHR) const
 bool                VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice) const
 QueueFamilyIndices  VulkanDevice::FindQueueFamilies(VkPhysicalDevice, VkSurfaceKHR) const
 bool                VulkanDevice::CreateLogicalDevice()
 bool                VulkanDevice::CreateAllocator(VkInstance)
 SwapchainSupportDetails VulkanDevice::QuerySwapchainSupport(VkSurfaceKHR) const

────────────────────────────────────────────────────────────────────────────────
 VulkanSwapchain.cpp
────────────────────────────────────────────────────────────────────────────────
 bool               VulkanSwapchain::Initialize(VulkanDevice&, VkSurfaceKHR, uint32_t w, uint32_t h)
 void               VulkanSwapchain::Shutdown()
 bool               VulkanSwapchain::Recreate(uint32_t width, uint32_t height)
 bool               VulkanSwapchain::CreateSwapchain(uint32_t width, uint32_t height)
 void               VulkanSwapchain::DestroySwapchainResources()
 VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(const vector<VkSurfaceFormatKHR>&) const
 VkPresentModeKHR   VulkanSwapchain::ChoosePresentMode(const vector<VkPresentModeKHR>&) const
 VkExtent2D         VulkanSwapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR&, uint32_t w, uint32_t h) const

────────────────────────────────────────────────────────────────────────────────
 VulkanBuffer.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanBuffer::Create(VulkanDevice&, VkDeviceSize, VkBufferUsageFlags, VmaMemoryUsage)
 void VulkanBuffer::Shutdown()
 bool VulkanBuffer::CreateStaging(VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::CreateVertex (VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::CreateIndex  (VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::CreateUniform(VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset)
 void VulkanBuffer::RecordCopyTo(VkCommandBuffer, VulkanBuffer& dst, VkDeviceSize size,
                                  VkDeviceSize srcOffset, VkDeviceSize dstOffset) const

────────────────────────────────────────────────────────────────────────────────
 VulkanImage.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanImage::Create(VulkanDevice&, uint32_t w, uint32_t h, VkFormat,
                           VkImageUsageFlags, VkImageAspectFlags)
 void VulkanImage::Shutdown()
 void VulkanImage::RecordLayoutTransition(VkCommandBuffer, VkImageLayout old, VkImageLayout new)
 void VulkanImage::RecordCopyFromBuffer(VkCommandBuffer, VulkanBuffer& src, uint32_t w, uint32_t h)

────────────────────────────────────────────────────────────────────────────────
 VulkanPipeline.cpp
────────────────────────────────────────────────────────────────────────────────
 bool           VulkanPipeline::Initialize(VulkanDevice&, VkFormat, const ShaderInfo&, const VertexInputInfo&)
 void           VulkanPipeline::Shutdown()
 bool           VulkanPipeline::CreateRenderPass(VkFormat swapchainFormat)
 bool           VulkanPipeline::CreatePipelineLayout()
 bool           VulkanPipeline::CreateGraphicsPipeline(const ShaderInfo&, const VertexInputInfo&)
 VkShaderModule VulkanPipeline::CreateShaderModule(const vector<uint32_t>& spirv)

────────────────────────────────────────────────────────────────────────────────
 VulkanCommand.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanCommand::Initialize(VulkanDevice&)
 void VulkanCommand::Shutdown()
 bool VulkanCommand::BeginRecording(uint32_t frameIndex)
 bool VulkanCommand::EndRecording  (uint32_t frameIndex)

────────────────────────────────────────────────────────────────────────────────
 VulkanSync.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanSync::Initialize(VulkanDevice&)
 void VulkanSync::Shutdown()

────────────────────────────────────────────────────────────────────────────────
 VCKExpansion.cpp
────────────────────────────────────────────────────────────────────────────────

 RECOMMENDED EXPANSION SHUTDOWN ORDER (before base objects):
   modelPipeline.Shutdown()     VulkanModelPipeline          ← owns descriptor layouts; shut down before pipeline
   ubo.Shutdown()               VulkanUniformSet<T>
   descAllocator.Shutdown()     VulkanDescriptorAllocator    ← pool destruction frees all allocated sets implicitly
   pool.Shutdown()              VulkanDescriptorPool
   vkDestroyDescriptorSetLayout(...)
   mesh.Shutdown()              VulkanMesh
   tex.Shutdown()               VulkanTexture
   depth.Shutdown()             VulkanDepthBuffer
   fbs.Shutdown()               VulkanFramebufferSet
   ── then base objects ──

 VulkanOneTimeCommand
   bool            VulkanOneTimeCommand::Begin(VulkanDevice&, VulkanCommand&)
   void            VulkanOneTimeCommand::End()

 VulkanFramebufferSet
   bool            VulkanFramebufferSet::Initialize(VulkanDevice&, VulkanSwapchain&,
                                                     VkRenderPass, VkImageView)
   void            VulkanFramebufferSet::Shutdown()
   bool            VulkanFramebufferSet::Recreate(VkRenderPass, VkImageView)
   VkFramebuffer   VulkanFramebufferSet::Get(uint32_t imageIndex) const
   (private) bool  VulkanFramebufferSet::CreateAll(VkRenderPass, VkImageView)
   (private) void  VulkanFramebufferSet::DestroyAll()

 VulkanDepthBuffer
   bool            VulkanDepthBuffer::Initialize(VulkanDevice&, uint32_t w, uint32_t h)
   void            VulkanDepthBuffer::Shutdown()
   bool            VulkanDepthBuffer::Recreate(uint32_t w, uint32_t h)

 VulkanSampler
   bool            VulkanSampler::CreateNearest(VulkanDevice&)
   bool            VulkanSampler::CreateLinear(VulkanDevice&)
   void            VulkanSampler::Shutdown()
   (private) bool  VulkanSampler::Create(VulkanDevice&, VkFilter, VkSamplerMipmapMode)

 VulkanTexture
   bool            VulkanTexture::CreateFromPixels(VulkanDevice&, VulkanCommand&,
                                                    const uint8_t*, uint32_t w, uint32_t h)
   void            VulkanTexture::Shutdown()

 VulkanMesh
   bool            VulkanMesh::Upload(VulkanDevice&, VulkanCommand&,
                                       const void* vertices, VkDeviceSize vertexSize,
                                       const uint32_t* indices, uint32_t indexCount)
   void            VulkanMesh::Shutdown()
   void            VulkanMesh::RecordDraw(VkCommandBuffer) const

 VulkanDescriptorLayoutBuilder
   VulkanDescriptorLayoutBuilder& VulkanDescriptorLayoutBuilder::Add(
                                      uint32_t, VkDescriptorType, VkShaderStageFlags)
   VkDescriptorSetLayout          VulkanDescriptorLayoutBuilder::Build(VulkanDevice&) const

 VulkanDescriptorPool
   bool            VulkanDescriptorPool::Initialize(VulkanDevice&,
                                                     VkDescriptorSetLayout,
                                                     VkDescriptorType)
   void            VulkanDescriptorPool::Shutdown()

 VulkanUniformSet<T>   (header-inline template)
   bool            VulkanUniformSet<T>::Initialize(VulkanDevice&,
                                                    VulkanDescriptorPool&,
                                                    uint32_t binding)
   void            VulkanUniformSet<T>::Shutdown()
   void            VulkanUniformSet<T>::Write(uint32_t frameIndex, const T& data)
   VkDescriptorSet VulkanUniformSet<T>::GetSet(uint32_t frameIndex) const

 VulkanDescriptorAllocator
   bool            VulkanDescriptorAllocator::Initialize(VulkanDevice&,
                                                          uint32_t maxSets,
                                                          initializer_list<PoolSize> sizes)
   void            VulkanDescriptorAllocator::Shutdown()
   VkDescriptorSet VulkanDescriptorAllocator::Allocate(VkDescriptorSetLayout)

 VulkanModelPipeline
   bool            VulkanModelPipeline::Initialize(VulkanDevice&, VkRenderPass,
                                                    const VulkanPipeline::ShaderInfo&,
                                                    const VulkanPipeline::VertexInputInfo&)
   void            VulkanModelPipeline::Shutdown()
   (private) bool  VulkanModelPipeline::BuildDescriptorLayouts()
   (private) bool  VulkanModelPipeline::BuildPipelineLayout()
   (private) bool  VulkanModelPipeline::BuildGraphicsPipeline(VkRenderPass,
                                                               const ShaderInfo&,
                                                               const VertexInputInfo&)
   (private) VkShaderModule VulkanModelPipeline::CreateShaderModule(const vector<uint32_t>&)

 VulkanMipmapGenerator
   static uint32_t VulkanMipmapGenerator::MipLevels(uint32_t width, uint32_t height)
   static bool     VulkanMipmapGenerator::IsFormatSupported(VulkanDevice&, VkFormat)
   bool            VulkanMipmapGenerator::Generate(VulkanDevice&, VulkanCommand&,
                                                    VkImage, uint32_t w, uint32_t h,
                                                    uint32_t mipLevels)

*/


#include "VCKExpansion.h"

// =============================================================================
//  END OF VCK.h
// =============================================================================
