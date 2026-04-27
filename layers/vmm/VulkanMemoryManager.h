#pragma once

// VCK.h is a single mega-header (pragma once) that defines
// VulkanDevice, VulkanCommand, VulkanSync, MAX_FRAMES_IN_FLIGHT, LogVk, VK_CHECK, etc.
// Including individual sub-headers instead would cause redefinition errors when
// App.cpp (or any TU that already includes VCK.h) also includes this file.
#include "../../VCK.h"

// =============================================================================
//  VulkanMemoryManager.h  (VMM)
//
//  Three-layer GPU resource lifecycle and memory management system for
//  VCK.  Include this file to get the full VMM API.
//
//  ┌──────────────────────────────────────────────────────────────────────┐
//  │  LAYER 1 - RAW ALLOCATION (VmmRawAlloc)                              │
//  │  Thin typed wrappers around VMA.  Creates buffers and images.        │
//  │  No knowledge of frames, lifetimes, or render loops.                 │
//  ├──────────────────────────────────────────────────────────────────────┤
//  │  LAYER 2 - RESOURCE LIFETIME TRACKING (VmmRegistry)                  │
//  │  Every allocation carries a Lifetime tag and ResourceInfo metadata.  │
//  │  The registry knows what exists, why it exists, and when to free it. │
//  ├──────────────────────────────────────────────────────────────────────┤
//  │  LAYER 3 - POOLS / STRATEGIES (VulkanMemoryManager)                  │
//  │  High-level allocation policies:                                     │
//  │    • StagingRing   - CPU→GPU ring buffer, zero per-frame realloc     │
//  │    • TransientPool - per-frame scratch; reset each frame             │
//  │    • PersistentPool- textures, static meshes; lives until shutdown   │
//  └──────────────────────────────────────────────────────────────────────┘
//
//  USAGE SKETCH
//  ────────────
//    VulkanMemoryManager vmm;
//    vmm.Initialize(device, command, totalStagingBytes);
//
//    // persistent allocation (lives until shutdown):
//    VmmBuffer vbo = vmm.AllocPersistent("terrain_vbo",
//        size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
//
//    // stage data into it via the ring:
//    vmm.StageToBuffer(vbo, data, size);
//
//    // transient allocation (auto-freed next frame):
//    VmmBuffer dynBuf = vmm.AllocTransient(frameIndex, "ui_verts",
//        size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
//    dynBuf.Upload(data, size);
//
//    // frame boundary:
//    vmm.BeginFrame(frameIndex);   // resets transient pool for this slot
//    vmm.EndFrame(frameIndex);     // flushes staging ring, frees expired resources
//
//    vmm.Shutdown();
//
//  INIT / SHUTDOWN ORDER
//  ─────────────────────
//  vmm.Initialize() - after VulkanDevice and VulkanCommand are ready
//  vmm.Shutdown()   - before VulkanCommand and VulkanDevice shutdown
// =============================================================================

#include "vk_mem_alloc.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>


namespace VCK {


// =============================================================================
//  LAYER 1 - RAW ALLOCATION
//  VmmRawAlloc
//
//  Thin typed wrappers around vmaCreateBuffer / vmaCreateImage.
//  No lifetime tracking, no frame awareness.  The output types (VmmBuffer,
//  VmmImage) carry the raw Vulkan + VMA handles and a mapped pointer for
//  host-visible allocations.
//
//  These are value types - they do NOT own the underlying GPU memory.
//  Lifetime management is the responsibility of Layer 2 (VmmRegistry) or
//  the caller if using raw alloc directly.
// =============================================================================

// -----------------------------------------------------------------------------
//  VmmBuffer - raw buffer handle + optional persistent map pointer
// -----------------------------------------------------------------------------
struct VmmBuffer
{
    VkBuffer      buffer    = VK_NULL_HANDLE;
    VmaAllocation alloc     = VK_NULL_HANDLE;
    VkDeviceSize  size      = 0;
    void*         mapped    = nullptr;   // non-null only for HOST_VISIBLE allocs
    uint32_t      id        = 0;         // registry ID (0 = untracked)

    bool IsValid()  const { return buffer != VK_NULL_HANDLE; }
    bool IsMapped() const { return mapped != nullptr; }

    // Convenience: memcpy into persistently-mapped memory.
    // Only valid when IsMapped() == true (staging ring, uniform, transient).
    void Upload(const void* data, VkDeviceSize bytes, VkDeviceSize offset = 0) const
    {
        if (mapped)
            std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, static_cast<size_t>(bytes));
    }
};

// -----------------------------------------------------------------------------
//  VmmImage - raw image handle + view
// -----------------------------------------------------------------------------
struct VmmImage
{
    VkImage       image     = VK_NULL_HANDLE;
    VkImageView   view      = VK_NULL_HANDLE;
    VmaAllocation alloc     = VK_NULL_HANDLE;
    VkFormat      format    = VK_FORMAT_UNDEFINED;
    uint32_t      width     = 0;
    uint32_t      height    = 0;
    uint32_t      mipLevels = 1;
    uint32_t      id        = 0;         // registry ID (0 = untracked)

    bool IsValid() const { return image != VK_NULL_HANDLE; }
};

// -----------------------------------------------------------------------------
//  VmmRawAlloc - stateless factory; caller owns the result
// -----------------------------------------------------------------------------
class VmmRawAlloc
{
public:
    // Buffer - generic
    static VmmBuffer CreateBuffer(VulkanDevice&      device,
                                  VkDeviceSize       size,
                                  VkBufferUsageFlags usage,
                                  VmaMemoryUsage     memUsage,
                                  bool               persistentMap = false);

    // Buffer - named convenience variants
    static VmmBuffer CreateStaging  (VulkanDevice& device, VkDeviceSize size); // CPU_ONLY + TRANSFER_SRC, mapped
    static VmmBuffer CreateVertex   (VulkanDevice& device, VkDeviceSize size); // GPU_ONLY + VERTEX | TRANSFER_DST
    static VmmBuffer CreateIndex    (VulkanDevice& device, VkDeviceSize size); // GPU_ONLY + INDEX  | TRANSFER_DST
    static VmmBuffer CreateUniform  (VulkanDevice& device, VkDeviceSize size); // CPU_TO_GPU + UNIFORM, mapped

    // Image - generic
    static VmmImage  CreateImage(VulkanDevice&      device,
                                 uint32_t           width,
                                 uint32_t           height,
                                 VkFormat           format,
                                 VkImageUsageFlags  usage,
                                 VkImageAspectFlags aspect,
                                 uint32_t           mipLevels = 1);

    // Free - match the type created
    static void FreeBuffer(VulkanDevice& device, VmmBuffer& buf);
    static void FreeImage (VulkanDevice& device, VmmImage&  img);
};


// =============================================================================
//  LAYER 2 - RESOURCE LIFETIME TRACKING
//  VmmRegistry
//
//  Every allocation made through the VMM is registered here with a Lifetime
//  tag and a ResourceInfo struct.  The registry knows what exists and why.
//
//  Lifetime semantics:
//    TransientFrame  - auto-freed at EndFrame() for that frame slot
//    FrameBuffered   - one copy per MAX_FRAMES_IN_FLIGHT slot; all freed at shutdown
//    Persistent      - freed at shutdown (or explicit Free call)
//    Manual          - registry tracks it for debug/stats only; caller frees
// =============================================================================

enum class Lifetime : uint8_t
{
    TransientFrame,   // exists only for frame N; freed at the start of the NEXT frame N
    FrameBuffered,    // one instance per frame slot (e.g. UBOs, per-frame staging)
    Persistent,       // lives until Shutdown() or explicit Free()
    Manual,           // caller manages destruction; registry tracks for visibility
};

struct ResourceInfo
{
    Lifetime    lifetime;
    uint32_t    createdFrame;    // absolute frame counter at creation time
    uint32_t    frameSlot;       // frame slot (0..MAX_FRAMES_IN_FLIGHT-1); 0 for non-frame resources
    const char* debugName;       // static string - not owned, not freed
};

// Internal entry types - one table for buffers, one for images
struct VmmBufferEntry { VmmBuffer buf; ResourceInfo info; };
struct VmmImageEntry  { VmmImage  img; ResourceInfo info; };

class VmmRegistry
{
public:
    VmmRegistry()  = default;
    ~VmmRegistry() = default;

    VmmRegistry(const VmmRegistry&)            = delete;
    VmmRegistry& operator=(const VmmRegistry&) = delete;

    void Initialize(VulkanDevice& device);
    void Shutdown();   // frees all Persistent + FrameBuffered entries

    // ── Registration ──────────────────────────────────────────────────────────
    // Registers an already-allocated VmmBuffer / VmmImage and returns its ID.
    // The registry takes ownership unless Lifetime::Manual.
    // Mutates buf.id / img.id so the caller's handle carries the registry key.
    uint32_t Register(VmmBuffer& buf, ResourceInfo info);
    uint32_t Register(VmmImage&  img, ResourceInfo info);

    // Removes an entry without freeing the underlying GPU resource.
    // Use after VmmRawAlloc::FreeBuffer / FreeImage (or vmm.FreeBuffer /
    // vmm.FreeImage) so the registry no longer tracks the freed handle.
    void UnregisterBuffer(uint32_t id);
    void UnregisterImage (uint32_t id);

    // ── Lookup ────────────────────────────────────────────────────────────────
    VmmBuffer* FindBuffer(uint32_t id);
    VmmImage*  FindImage (uint32_t id);

    // ── Lifetime events ───────────────────────────────────────────────────────
    // Call at the start of each frame to free TransientFrame entries that
    // were allocated during the PREVIOUS occurrence of this frame slot.
    void FreeTransient(uint32_t frameSlot);

    // Call at shutdown to free Persistent + FrameBuffered entries.
    // Called automatically by Shutdown().
    void FreeAll();

    // ── Debug ─────────────────────────────────────────────────────────────────
    void LogStats() const;   // prints a summary to VS Output via LogVk

    uint32_t BufferCount() const { return static_cast<uint32_t>(m_Buffers.size()); }
    uint32_t ImageCount()  const { return static_cast<uint32_t>(m_Images.size()); }

private:
    VulkanDevice* m_Device  = nullptr;
    uint32_t      m_NextId  = 1;   // 0 is reserved for "untracked"

    std::unordered_map<uint32_t, VmmBufferEntry> m_Buffers;
    std::unordered_map<uint32_t, VmmImageEntry>  m_Images;
};


// =============================================================================
//  LAYER 3 - POOLS / STRATEGIES
//  VulkanMemoryManager  (the public-facing VMM)
//
//  Three allocation strategies built on top of Layers 1 and 2:
//
//  StagingRing
//  ───────────
//  A single large persistently-mapped CPU-visible buffer used as a ring.
//  Each StageToBuffer / StageToImage call claims a chunk of the ring,
//  records a vkCmdCopy* into the VMM's internal one-time command, and
//  advances the write head.  EndFrame() submits the batch and reclaims
//  space once the GPU has consumed it.
//
//  Why a ring?  Individual vmaCreateBuffer calls for every upload are slow
//  (driver sync, allocation overhead).  A ring amortises this to zero per
//  upload - the large allocation happens once at Initialize().
//
//  TransientPool
//  ─────────────
//  Per-frame-slot scratch memory for dynamic geometry, UI, debug draws.
//  Internally a fixed block of CPU_TO_GPU memory per slot; BeginFrame()
//  resets the offset to zero without re-allocating.  Allocations are
//  sub-allocated by offset and returned as VmmBuffer views into the block.
//  Overflow falls back to a one-off VmmRawAlloc call.
//
//  PersistentPool
//  ──────────────
//  Textures, static meshes, permanent render targets.  Each allocation is a
//  separate vmaCreateBuffer / vmaCreateImage call registered as
//  Lifetime::Persistent in the registry.  Freed explicitly or at Shutdown().
// =============================================================================

class VulkanMemoryManager
{
public:
    // ── Config ────────────────────────────────────────────────────────────────
    struct Config
    {
        VkDeviceSize stagingRingSize      = 32 * 1024 * 1024;  // 32 MB staging ring
        VkDeviceSize transientBlockSize   =  8 * 1024 * 1024;  //  8 MB per frame slot
    };

    VulkanMemoryManager()  = default;
    ~VulkanMemoryManager() = default;

    VulkanMemoryManager(const VulkanMemoryManager&)            = delete;
    VulkanMemoryManager& operator=(const VulkanMemoryManager&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Call after VulkanDevice and VulkanCommand are initialised.
    bool Initialize(VulkanDevice& device, VulkanCommand& command,
                    const Config& config);

    // Call before VulkanCommand and VulkanDevice shutdown.
    void Shutdown();

    // ── Frame boundary ────────────────────────────────────────────────────────
    // BeginFrame - resets the transient pool for this slot, frees expired
    //              TransientFrame resources from the previous cycle.
    // EndFrame   - submits any pending staging commands, advances ring tail.
    void BeginFrame(uint32_t frameIndex, uint32_t absoluteFrame);
    void EndFrame  (uint32_t frameIndex);

    // ── Staging ring - CPU → GPU transfers ────────────────────────────────────
    // Records a copy from CPU data into a GPU-side destination buffer.
    // The copy is batched into the current frame's staging command buffer
    // and submitted at EndFrame().  Safe to call multiple times per frame.
    // Returns false if the ring is full (increase stagingRingSize).
    bool StageToBuffer(VmmBuffer&  dst,
                       const void* data,
                       VkDeviceSize size,
                       VkDeviceSize dstOffset = 0);

    bool StageToImage (VmmImage&   dst,
                       const void* data,
                       VkDeviceSize size,
                       uint32_t     width,
                       uint32_t     height);

    // Flush any pending staging commands immediately (blocking - use sparingly).
    // Useful during Init() to upload persistent resources before the render loop.
    void FlushStaging();

    // ── Transient pool - per-frame scratch ────────────────────────────────────
    // Returns a sub-allocated view into the current frame's transient block.
    // The returned VmmBuffer shares the underlying VkBuffer - do NOT call
    // VmmRawAlloc::FreeBuffer on it.  It is auto-reset at the next BeginFrame.
    // debugName must outlive the frame (static or literal strings are ideal).
    VmmBuffer AllocTransient(uint32_t     frameIndex,
                             const char*  debugName,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage
                                 = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // ── Persistent pool - long-lived resources ────────────────────────────────
    VmmBuffer AllocPersistent(const char*        debugName,
                              VkDeviceSize       size,
                              VkBufferUsageFlags usage);

    VmmImage  AllocPersistentImage(const char*        debugName,
                                   uint32_t           width,
                                   uint32_t           height,
                                   VkFormat           format,
                                   VkImageUsageFlags  usage,
                                   VkImageAspectFlags aspect,
                                   uint32_t           mipLevels = 1);

    // Explicit free for Persistent resources before Shutdown().
    void FreeBuffer(VmmBuffer& buf);
    void FreeImage (VmmImage&  img);

    // ── Registry access ───────────────────────────────────────────────────────
    // Direct access to the registry for debug tools / stats.
    VmmRegistry& Registry() { return m_Registry; }

    // ── Debug ─────────────────────────────────────────────────────────────────
    void LogStats() const;

private:
    // ── Internal staging ring state ───────────────────────────────────────────
    //
    //  Current model: SubmitStagingCmd() ends with vkQueueWaitIdle(), which
    //  means that by the time SubmitStagingCmd() returns, ALL uploads recorded
    //  into the staging command are finished on the GPU.  EndFrame() and
    //  FlushStaging() therefore fully reset writeHead/inFlight right after
    //  submit - the ring is trivially drained every frame.
    //
    //  frameTails[] is retained as diagnostic / forward-compat metadata for a
    //  future non-blocking staging model (fence-per-frame), but is not
    //  currently used for reclamation.
    struct StagingRing
    {
        VmmBuffer  buffer;         // the ring buffer itself
        VkDeviceSize capacity  = 0;
        VkDeviceSize writeHead = 0;  // next byte to write into
        VkDeviceSize inFlight  = 0;  // bytes submitted but not yet retired

        // Per-frame "tail" positions (diagnostic; see note above).
        std::array<VkDeviceSize, MAX_FRAMES_IN_FLIGHT> frameTails{};

        bool HasSpace(VkDeviceSize needed) const;
        // Returns the offset at writeHead and advances it (with alignment).
        VkDeviceSize Claim(VkDeviceSize size, VkDeviceSize alignment = 4);
        // Fully reset the ring - valid only when the GPU is idle on staging work.
        void         Reset();
    };

    // ── Internal transient pool state ─────────────────────────────────────────
    struct TransientBlock
    {
        VmmBuffer    buffer;      // the block buffer (CPU_TO_GPU, VERTEX | STORAGE | UNIFORM)
        VkDeviceSize capacity = 0;
        VkDeviceSize offset   = 0;  // current allocation cursor; reset each frame

        void Reset() { offset = 0; }
        bool HasSpace(VkDeviceSize needed, VkDeviceSize alignment = 256) const;
        VkDeviceSize Claim(VkDeviceSize size, VkDeviceSize alignment = 256);
    };

    // ── Internal staging command recording ───────────────────────────────────
    bool EnsureStagingCmd();     // opens m_StagingCmd if not already open
    void SubmitStagingCmd();     // ends + submits + waits, resets cmd

    // ── State ─────────────────────────────────────────────────────────────────
    VulkanDevice*  m_Device  = nullptr;
    VulkanCommand* m_Command = nullptr;
    Config         m_Config;

    VmmRegistry    m_Registry;
    StagingRing    m_Ring;

    std::array<TransientBlock, MAX_FRAMES_IN_FLIGHT> m_Transient{};

    // Staging uses a dedicated one-time command buffer (not the per-frame ones).
    //
    // v0.3: VMM owns its own VkCommandPool bound to the transfer queue family
    // (which may be dedicated or aliased to graphics).  This is required by
    // VUID-vkQueueSubmit-pCommandBuffers-00074: the pool a command buffer was
    // allocated from must belong to the same queue family as the queue it is
    // submitted to.  Using VulkanCommand's graphics pool here would violate
    // the spec on AMD/NVIDIA (dedicated transfer family) and trigger GPU
    // hang / corruption.
    VkCommandPool   m_TransferPool   = VK_NULL_HANDLE;
    uint32_t        m_TransferFamily = 0;
    uint32_t        m_GraphicsFamily = 0;
    VkCommandBuffer m_StagingCmd     = VK_NULL_HANDLE;
    bool            m_StagingOpen    = false;

    // Queue ownership transfer tracking (v0.3).  When transferFamily differs
    // from graphicsFamily, every StageTo* records a release half on the
    // transfer queue and pushes a matching acquire half here.  After the
    // transfer submit completes, SubmitStagingCmd allocates a short command
    // buffer from the graphics-family pool, records all acquire barriers,
    // and submits it to the graphics queue (CPU-serialised: the transfer
    // fence is waited on before the graphics submit, which satisfies the
    // execution dependency required between release and acquire per Vulkan
    // spec section 7.7.4).  Empty when families alias.
    struct PendingAcquireBuffer { VkBuffer     buffer;     VkDeviceSize size; VkDeviceSize offset; };
    struct PendingAcquireImage  { VkImage      image;      uint32_t     mipLevels; };
    std::vector<PendingAcquireBuffer> m_PendingAcquireBuffers;
    std::vector<PendingAcquireImage>  m_PendingAcquireImages;

    bool FamiliesDiffer() const { return m_TransferFamily != m_GraphicsFamily; }

    uint32_t m_AbsoluteFrame = 0;
};


} // namespace VCK
