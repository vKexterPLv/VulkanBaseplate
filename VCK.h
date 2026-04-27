// =============================================================================
//  VCK.h  -  Vulkan Core Kit
//
//  This header is the SINGLE SOURCE OF TRUTH for the VCK public API.
//  Every class, free function, config knob, enum, macro and design rule
//  that the user is expected to know about is documented here.  The .h
//  / .cpp files under layers/ carry only a one-line "what am I" comment
//  and link back to this file.  Long-form guides live in docs/ and the
//  GitHub wiki; this file is deliberately the only place that tries to
//  document the surface in depth.
//
//  REPOSITORY LAYOUT
//  ─────────────────
//  VCK.h                         (this file, main include)
//  layers/
//      core/                     - primitive Vulkan wrappers +
//                                  VCK::Window cross-platform facade.
//          VCKCrossplatform.{h,cpp}   VCK::Window, WindowCreateInfo,
//                                      VCK_PLATFORM_* detection macros.
//          VulkanHelpers.h            VCKLog, VK_CHECK, Config, enums.
//          VulkanContext.{h,cpp}      VkInstance + surface (+ debug).
//          VulkanDevice.{h,cpp}       VkDevice + queues + features.
//          VulkanSwapchain.{h,cpp}    VkSwapchain + MSAA + AA detect.
//          VulkanBuffer.{h,cpp}       Buffers, vertex/index/uniform.
//          VulkanImage.{h,cpp}        2D/MSAA images + attachments.
//          VulkanPipeline.{h,cpp}     Graphics pipeline + A2C + SRS.
//          VulkanCommand.{h,cpp}      Command pool + primaries.
//          VulkanSync.{h,cpp}         Per-frame semaphores + fences.
//          VmaImpl.cpp                VMA single TU (VMA_IMPLEMENTATION).
//      expansion/                - reusable rendering building blocks.
//          VCKExpansion.{h,cpp}       Classes [1]-[12] + [23]-[25] +
//                                      HandleLiveResize overloads.
//          VCKMath.h                  Vec2/3/4, Mat4 + free helpers.
//      execution/                - frame scheduling & observability.
//          VCKExecution.{h,cpp}       Classes [13]-[22] + timeline-aware
//                                      and scheduler-aware HandleLiveResize
//                                      overloads.
//      vmm/                      - memory manager (optional).
//          VulkanMemoryManager.{h,cpp}
//  vendor/                       - source-only third-party deps (in repo).
//      vulkan_headers/vulkan/    - Vulkan SDK headers, vendored.
//      glfw/include/GLFW/        - GLFW C API headers (used by VCKCrossplatform).
//      vma/vk_mem_alloc.h        - AMD VMA allocator (used by VmaImpl.cpp).
//  example/
//      deps/                     - binary-only Windows build assets.
//          libglfw3.a            - GLFW pre-compiled MinGW lib (download,
//                                  not source - Linux/macOS use pkg-config).
//      build.bat / build.sh      - Windows / Linux+macOS build scripts.
//      <13 example dirs>         - see example/README.md or docs/Examples.md.
//                                   Menu now runs [1]-[13], [A] builds all.
//  docs/                         - design, build, examples, API reference.
//                                  Start with docs/Overview.md for the
//                                  one-page "what VCK is / gives / never
//                                  takes / modern v0.3 optimisations" tour.
//  .github/workflows/build.yml   - CI (Windows runner, build.bat [A]).
//
//  LAYERING
//  ────────
//      layers/core/       instance, device, swapchain, pipeline, sync.
//          ↓
//      layers/expansion/  textures, meshes, descriptors, framebuffers,
//                         HandleLiveResize (base + depth).
//          ↓
//      layers/execution/  FrameScheduler, JobGraph, DebugTimeline,
//                         timeline-aware HandleLiveResize.
//          ↓
//      layers/vmm/        (optional) allocator policies + lifetime tags.
//          ↓
//      your renderer / game / tool
//
//  Each layer only depends on the layers above it.  Expansion never
//  references execution.  Core never references expansion.
//
//  INIT / SHUTDOWN ORDER
//  ─────────────────────
//  Init:      Window → Context → Device → Swapchain → Pipeline
//             → Command → Sync → (expansion resources) → (frame loop)
//  Shutdown:  Sync → Command → Pipeline → Swapchain → Device → Context
//             (expansion resources and VMM allocations must be destroyed
//             BEFORE the core object they reference)
//
//  NAMESPACE
//  ─────────
//  Everything VCK exposes lives in:  namespace VCK { ... }
//  LogVk and VK_CHECK are deliberately at global scope so every TU can
//  use them without a `using` declaration.
//
//  ONE-HOUR QUICK START (HELLO VCK)
//  ────────────────────────────────
//    #include "VCK.h"
//
//    int main() {
//      // 1. Window (GLFW under the hood; one-shot platform init inside).
//      VCK::Window window;
//      VCK::WindowCreateInfo wci;
//      wci.width = 1280; wci.height = 720; wci.title = "Hello VCK";
//      wci.resizable = true;
//      window.Create(wci);
//
//      // 2. Optional non-default knobs.  Leave for defaults to get:
//      //    AATechnique::Auto (first-run 5-step detect),
//      //    Mailbox→FIFO present, framesInFlight=2, debug=false.
//      VCK::Config cfg;
//      cfg.swapchain.presentMode = VCK::PresentMode::Mailbox;
//      cfg.sync.framesInFlight   = 3;
//      // cfg.debug = true;   // VCKLog Info lines only surface when true.
//
//      // 3. Init chain in order.  Every step has a zero-Config overload.
//      VCK::VulkanContext   ctx;  ctx .Initialize(window, cfg);
//      VCK::VulkanDevice    dev;  dev .Initialize(ctx,    cfg);
//      VCK::VulkanSwapchain sc;   sc  .Initialize(dev, ctx,
//                                              window.GetWidth(),
//                                              window.GetHeight(), cfg);
//      VCK::VulkanPipeline  pipe; pipe.Initialize(dev, sc, shaders, vi);
//      VCK::VulkanCommand   cmd;  cmd .Initialize(dev, cfg);
//      VCK::VulkanSync      sync; sync.Initialize(dev, cfg);
//      VCK::VulkanFramebufferSet fb; fb.Initialize(dev, sc, pipe);
//
//      // 4. Per-frame loop.  Drive it yourself, or hand off to
//      //    VCK::FrameScheduler for automatic pipelining.
//      while (!window.ShouldClose()) {
//        window.PollEvents();
//        if (window.IsMinimized()) { window.WaitEvents(); continue; }
//        VCK::HandleLiveResize(window, dev, sc, fb, pipe);
//        // ... record commands + submit + present ...
//      }
//
//      // 5. Reverse teardown.  Expansion/VMM first, then core, then window.
//      fb.Shutdown(); sync.Shutdown(); cmd.Shutdown();
//      pipe.Shutdown(); sc.Shutdown(); dev.Shutdown(); ctx.Shutdown();
//      window.Destroy();
//    }
//
//  See example/HelloExample for the full compilable version, and the
//  wiki page "Build your first app" for a per-line walkthrough of why
//  each call exists.
//
//  CLASS / FREE-FUNCTION INDEX
//  ───────────────────────────
//  layers/core/
//    VCK::Window                    cross-platform window + input.
//    VCK::VulkanContext             VkInstance + debug + surface.
//    VCK::VulkanDevice              VkPhysicalDevice + VkDevice.
//    VCK::VulkanSwapchain           VkSwapchainKHR + MSAA resolve +
//                                    GetAATechnique() / GetAACfg().
//    VCK::VulkanBuffer              generic VkBuffer wrapper.
//    VCK::VulkanImage               2D / MSAA image + view.
//    VCK::VulkanPipeline            VkPipeline + A2C + SampleRateShading.
//    VCK::VulkanCommand             command pool + frame primaries.
//    VCK::VulkanSync                image-available / render-finished /
//                                    in-flight fences, per frame.
//    VCK::VCKLog                    Info / Notice / Warn / Error with
//                                    dedup + SetDebug(bool).
//    VCK::DetectRecommendedAA(...)  5-step AA decision tree (tier →
//                                    forward → motion vectors → pick).
//    VCK::AATechnique { Auto, Off, MSAA, MSAA_A2C, SampleRate,
//                       FXAA, SMAA_1x, SMAA_T2x, TAA, TAAU };
//  layers/expansion/
//    [1]  VulkanOneTimeCommand       scoped single-submit command buffer.
//    [2]  VulkanFramebufferSet       one framebuffer per swapchain image.
//    [3]  VulkanDepthBuffer          depth attachment + format pick.
//    [4]  VulkanSampler              VkSampler wrapper + defaults.
//    [5]  VulkanTexture              sampled 2D image + staging upload.
//    [6]  VulkanMesh                 vertex + index buffer bundle.
//    [7]  VulkanDescriptorLayoutBuilder
//    [8]  VulkanDescriptorPool
//    [9]  VulkanUniformSet<T>        per-frame uniform buffer slots.
//    [10] VulkanDescriptorAllocator  allocator around 7/8/9.
//    [11] VulkanModelPipeline        pipeline preset for textured model.
//    [12] VulkanMipmapGenerator      runtime mipmap blit utility.
//    [23] VertexLayout               named vertex-input builder.
//    [24] PushConstants              named push-constant block.
//    [25] Primitives                 Cube/Plane/Sphere/Quad/Line mesh builders.
//    VCK::Vec2/3/4 + Mat4           POD math primitives (VCKMath.h).
//    HandleLiveResize(window, dev, sc, fb, pipe)           base overload.
//    HandleLiveResize(window, dev, sc, fb, pipe, depth)    + depth buffer.
//    HandleLiveResize(window, sc, fb, pipe, scheduler)     scheduler-aware
//                                                           (v0.3: drains the
//                                                           scheduler's timeline
//                                                           or fences instead
//                                                           of vkDeviceWaitIdle).
//    HandleLiveResize(window, sc, fb, pipe, depth, sch)    + depth buffer.
//    VulkanCommand::AllocateSecondary / BeginSecondary / EndSecondary /
//    FreeSecondary / ExecuteSecondaries                    secondary command
//                                                           buffer lifecycle
//                                                           (v0.3).
//    VulkanDevice::HasTimelineSemaphores()                 reports whether
//                                                           VK_KHR_timeline_semaphore
//                                                           is enabled at
//                                                           device-create time
//                                                           (v0.3).
//    VulkanDevice::GetComputeQueue / GetTransferQueue      dedicated queues,
//                                                           fall back to
//                                                           graphics queue +
//                                                           VCKLog::Notice
//                                                           when vendor does
//                                                           not expose them
//                                                           (v0.3).
//  layers/execution/
//    [13] FramePolicy / FrameConfig  Pipelined | Lockstep | AsyncMax.
//    [14] TimelineSemaphore          VK_KHR_timeline_semaphore wrapper.
//    [15] DependencyToken            cross-frame GPU dependency.
//    [16] QueueSet                   graphics + async compute + transfer.
//    [17] GpuSubmissionBatcher       coalesce vkQueueSubmit calls.
//    [18] BackpressureGovernor       cap frames-in-flight under stress.
//    [19] JobGraph                   DAG scheduler for frame sub-tasks.
//    [20] DebugTimeline              observable span log + Dump() +
//                                     DumpChromeTracing("trace.json").
//    [21] Frame                      per-frame handle: cmd + semaphores.
//    [22] FrameScheduler             the whole loop as one object.
//    HandleLiveResize(window, dev, sc, fb, pipe, timeline)
//    HandleLiveResize(window, dev, sc, fb, pipe, depth, timeline)
//    FrameScheduler::FrameTimeline()                       the scheduler's
//                                                           per-scheduler
//                                                           TimelineSemaphore
//                                                           (v0.3).
//    FrameScheduler::SlotToken(slot)                       DependencyToken
//                                                           bound to the
//                                                           slot's last signalled
//                                                           timeline value.
//    FrameScheduler::DrainInFlight()                       wait on every slot's
//                                                           most recent submit
//                                                           without touching
//                                                           vkDeviceWaitIdle
//                                                           (v0.3).
//  layers/vmm/ (optional)
//    VmmRawAlloc / VmmRegistry / VulkanMemoryManager - see its header.
//
//  CONFIG KNOBS (VCK::Config)
//  ──────────────────────────
//  debug                           surfaces VCKLog Info lines when true.
//  context.{appName, validation}   instance-layer config.
//  device.{preferDiscreteGpu,      physical-device selection + extras.
//          extraDeviceExtensions,
//          queuePref,
//          enableTimelineSemaphores,  v0.3: chain VK_KHR_timeline_semaphore
//          enableDedicatedComputeQueue,  when the device supports it; when
//          enableDedicatedTransferQueue, off VulkanDevice::HasTimeline-
//          enableBindless}             Semaphores()/GetComputeQueue()/
//                                     GetTransferQueue() fall back to the
//                                     graphics queue (rule 19).
//                                     enableBindless (post-v0.3, R24): when
//                                     true requests VK_EXT_descriptor_indexing
//                                     and announces it via R23 Notice.  Stage-1
//                                     surface only - bindless descriptor
//                                     helpers ship in v0.4.
//  rendering.{mode}                post-v0.3 (R24): mode = Classic (default,
//                                     VkRenderPass + VkFramebuffer) or
//                                     Dynamic (requests VK_KHR_dynamic_
//                                     rendering; codepath ships in v0.4 -
//                                     today the request is acknowledged via
//                                     R23 Notice and rendering stays Classic).
//  swapchain.{presentMode,         Mailbox / Fifo / Immediate / FifoLatest-
//             msaaSamples,         Ready (post-v0.3, R24: requests
//             preferredSurfaceFmt}    VK_EXT_present_mode_fifo_latest_ready;
//                                     falls back to FIFO with a Warn when
//                                     unavailable, fully wired through
//                                     VulkanDevice + VulkanSwapchain).
//                                     msaaSamples: 1/2/4/8 or MSAA_AUTO
//                                     sentinel (pick from device).
//  sync.{framesInFlight}           clamped to [1, MAX_FRAMES_IN_FLIGHT].
//  aa.{technique, alphaToCoverage, technique=Auto runs detector once;
//      sampleRateShading,          user inputs forwardRenderer + motion
//      minSampleShading,           vectors so VCK can pick an AA name.
//      forwardRenderer,
//      supportsMotionVectors}
//  pipeline.{alphaToCoverage,      both fed by aa.* if Auto picks MSAA.
//            sampleRateShading}
//
//  DESIGN RULES (SHORT FORM, see docs/Design.md for full text)
//  ───────────────────────────────────────────────────────────
//   1 Explicit > magic.     Init/Shutdown pairs, no singletons.
//   2 No ownership leaks.   Expansion/execution borrow core by pointer.
//   3 Strict lifecycle order (see INIT / SHUTDOWN ORDER above).
//   4 No hidden sync - only Shutdown() paths may call vkDeviceWaitIdle;
//     the runtime hot path never does (v0.3: scheduler-aware HandleLiveResize
//     drains via FrameScheduler::DrainInFlight, and VMM staging uses a
//     per-submit VkFence).  The legacy Swapchain::Recreate default still
//     calls vkDeviceWaitIdle unless the caller passes drainedExternally=true
//     (scheduler-aware path).
//   5 Frame-scoped or persistent; no orphan allocations.
//   6 No hidden behaviour.  Every non-trivial decision is logged.
//   7 User owns the frame, opt in to FrameScheduler.
//   8 Explicit synchronisation model.
//   9 Escape hatch: every class exposes its raw Vk* handle.
//  10 Zero hidden GPU state.  VCKLog dedup is a logger convention.
//  11 Deterministic frame behaviour (Pipelined/Lockstep).
//  12 Explicit recreation events - logged + timeline span.
//  13 Debuggability is a core feature (VCKLog + DebugTimeline).
//  14 Fail fast, fail loud.  Every failure returns an explicit bool AND
//     logs via VCKLog::Error("<subsystem>", ...).  VK_CHECK routes non-
//     VK_SUCCESS to VCKLog::Error regardless of cfg.debug.
//  15 Minimal core surface - VCK stops at the pipeline.
//  16 No engine assumptions - no scene graph, no material system.
//  17 Frame is the unit of truth.
//  18 External synchronisation.  VCK instances are externally synced
//     (Vulkan spec); concurrent access from multiple threads is UB
//     unless the caller locks.  JobGraph is the sole exception.
//  19 Zero cost for unused features.  Un-Initialize'd modules allocate
//     nothing, spawn no thread, emit no log line.
//  20 Every public class in VCK.h is exercised by at least one example
//     under example/.
//  21 VCK.h is the API surface.  Layer headers under layers/* are
//     implementation detail and may move.  Breaking changes to VCK.h
//     bump the minor version (0.x) until v1.0.0.
//  22 VCK never owns user handles.  Raw Vk* passed in is caller-owned;
//     handles VCK returns via getters are borrows, do not destroy them.
//  23 Extension transparency.  Every instance- / device-level extension
//     VCK enables silently is announced via VCKLog::Notice("Context"|
//     "Device", ...) at init, including extension name, support verdict,
//     and fallback path.  The user is never surprised by what is running
//     underneath - grep the init log for "ext " to see the full set.
//  24 cfg is the contract.  Every behavioural difference VCK can express
//     that the user can reasonably want to choose between lives in cfg.
//     Litmus test: "If it changes how the user writes their renderer ->
//     cfg.  If it changes how VCK works underneath -> silent bundle."
//     Today's silent bundle: VK_KHR_synchronization2, VK_KHR_buffer_
//     device_address, VK_EXT_memory_budget, VK_EXT_device_fault,
//     VK_KHR_present_wait, VK_KHR_present_id (probed + enabled when the
//     device advertises them, R23 Notice per result).  Today's cfg knobs
//     for extensions: cfg.rendering.mode, cfg.device.enableBindless,
//     cfg.swapchain.presentMode = FifoLatestReady (see entries above).
// =============================================================================


#pragma once

// ─── System / Vulkan prerequisites ───────────────────────────────────────────
// Platform detection lives in VCKCrossplatform.h - include it first so
// the VCK_PLATFORM_* macros are defined before we use them below.
#include "layers/core/VCKCrossplatform.h"

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
#include "layers/core/VulkanHelpers.h"   // VCKLog/LogVk, VK_CHECK, VCK::Config, PresentMode, QueuePreference, MAX_FRAMES_IN_FLIGHT

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

#include "layers/core/VCKMath.h"         // Vec2/3/4, Mat4, free functions (no Vulkan deps)
#include "layers/core/VulkanContext.h"
#include "layers/core/VulkanDevice.h"
#include "layers/core/VulkanSwapchain.h"
#include "layers/core/VulkanBuffer.h"
#include "layers/core/VulkanImage.h"
#include "layers/core/VulkanPipeline.h"
#include "layers/core/VulkanSync.h"
#include "layers/core/VulkanCommand.h"
// VCKCrossplatform.h (VCK::Window + VCK_PLATFORM_*) is included at the top of
// this file before any platform conditionals - see line ~90.




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
//  CLASSES  (20)
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
//  [23] VertexLayout                  - named vertex-input builder (VkVertex* structs)
//  [24] PushConstants                 - named push-constant block (Declare / Set / Apply)
//  [25] Primitives                    - Cube / Plane / Sphere / Quad / Line mesh builders
//  [26] ShaderLoader                  - loads SPIR-V from .spv files, optional GLSL via glslangValidator
//  [27] ShaderWatcher                 - polls .spv timestamps for hot reload (debug-only)
//  [28] SpecConstants                 - VkSpecializationInfo builder (specialization constants)
//  [29] ShaderStage                   - per-stage VertexLayout / PushConstants / binding declaration
//  [30] ShaderInterface               - merges ShaderStages into pipeline cfg + descriptor layouts
//
//  (Numbers [13]-[22] are used by the execution layer - see below.)
//
//  CORE MATHS (header-only, no Vulkan deps)
//  ────────────────────────────────────────
//  Vec2  / Vec3  / Vec4                plain POD, tightly packed (memcpy-safe).
//  Mat4                                column-major, 16 floats, identity default.
//  Translate / Rotate / Scale          affine builders.
//  Perspective / LookAt                Vulkan-ready (Y-flipped, depth [0,1]).
//  operator+ / - / * / Dot / Cross / Normalize / Length / Transpose / Radians / Degrees
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
 VulkanContext.cpp
────────────────────────────────────────────────────────────────────────────────
 bool        VulkanContext::Initialize(const Window&, const std::string& appName, const Config& = {})
 bool        VulkanContext::Initialize(HWND windowHandle, const std::string& appName)   // Windows-only legacy overload
 void        VulkanContext::Shutdown()
 bool        VulkanContext::CreateInstance(const std::string& appName, const std::vector<const char*>& surfaceExtensions)
 bool        VulkanContext::CreateDebugMessenger()
 bool        VulkanContext::CreateSurface(const Window&)
 bool        VulkanContext::CreateSurface(HWND windowHandle)                             // Windows-only legacy overload
 bool        VulkanContext::CheckValidationLayerSupport()
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
 bool               VulkanSwapchain::Recreate(uint32_t width, uint32_t height,
                                               bool drainedExternally = false)
 // v0.3: drainedExternally=true skips the internal vkDeviceWaitIdle; the
 // scheduler-aware HandleLiveResize overload sets it after DrainInFlight().
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
 // v0.3 secondary command buffer API (same pool, caller-serialised):
 VkCommandBuffer VulkanCommand::AllocateSecondary()
 void            VulkanCommand::FreeSecondary(VkCommandBuffer)
 bool            VulkanCommand::BeginSecondary(VkCommandBuffer,
                                                const VkCommandBufferInheritanceInfo&,
                                                VkCommandBufferUsageFlags extraFlags = 0)
 bool            VulkanCommand::EndSecondary(VkCommandBuffer)
 static void     VulkanCommand::ExecuteSecondaries(VkCommandBuffer primary,
                                                    const VkCommandBuffer* secondaries,
                                                    uint32_t count)

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

 VertexLayout  [23]  - named vertex-input builder.
   Call Add(name, VertexAttrType::...) once per attribute in declaration
   order.  Location indices start at 0 and increment; byte offsets pack
   tight; Stride() returns the running total.  Binding() builds one
   VkVertexInputBindingDescription, Attributes() builds the parallel
   vector of VkVertexInputAttributeDescription that VulkanModelPipeline's
   VertexInputInfo expects.  The two structs are plain-Vulkan values -
   VCK doesn't cache, doesn't store, doesn't own (rule 22).  Names are
   stored as raw pointers; pass string literals or values that outlive
   the layout.
     enum class VertexAttrType { Float, Vec2, Vec3, Vec4, Int, UInt };
     VertexLayout&                                  Add(const char* name, VertexAttrType t)
     uint32_t                                       Stride() const
     VkVertexInputBindingDescription                Binding(uint32_t binding = 0) const
     std::vector<VkVertexInputAttributeDescription> Attributes(uint32_t binding = 0) const
     std::size_t                                    Count() const
   Example:
     VCK::VertexLayout vl;
     vl.Add("position", VCK::VertexAttrType::Vec3)
       .Add("normal",   VCK::VertexAttrType::Vec3)
       .Add("uv",       VCK::VertexAttrType::Vec2);
     // Pass vl.Binding(0) + vl.Attributes(0) into VulkanModelPipeline.

 PushConstants  [24]  - named push-constant block.
   Declare(name, type) reserves a slot; Set(name, value) writes into the
   backing buffer with no hashing on the hot path (names are compared
   linearly - blocks are small).  Apply() emits one vkCmdPushConstants
   call covering the whole block.  Mismatched types or unknown names
   log through VCKLog::Error (rule 14) and leave the buffer unchanged.
     enum class PushConstType { Float, Vec2, Vec3, Vec4, Mat4, Int, UInt };
     PushConstants& Declare(const char* name, PushConstType t)
     PushConstants& Set(const char* name, float v)
     PushConstants& Set(const char* name, const Vec2&)
     PushConstants& Set(const char* name, const Vec3&)
     PushConstants& Set(const char* name, const Vec4&)
     PushConstants& Set(const char* name, const Mat4&)
     PushConstants& Set(const char* name, int32_t v)
     PushConstants& Set(const char* name, uint32_t v)
     uint32_t              Size() const
     VkPushConstantRange   Range(VkShaderStageFlags stages) const
     void                  Apply(VkCommandBuffer cb,
                                 VkPipelineLayout layout,
                                 VkShaderStageFlags stages) const
   Example:
     VCK::PushConstants pc;
     pc.Declare("mvp",   VCK::PushConstType::Mat4)
       .Declare("model", VCK::PushConstType::Mat4);
     pc.Set("mvp", proj * view * model).Set("model", model);
     pc.Apply(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT);

 Primitives  [25]  - namespace of CPU-side mesh builders.
   Each builder returns a Primitives::Mesh by value.  Caller owns the
   buffers (rule 22) and can upload via VulkanMesh.  Vertex layout is
   always position (Vec3) + normal (Vec3) + uv (Vec2), 32-bit indices.
   Line() uses line topology; the caller is responsible for setting
   VK_PRIMITIVE_TOPOLOGY_LINE_LIST on the pipeline (rule 16).
     struct Primitives::Mesh {
         std::vector<Vec3>     positions;
         std::vector<Vec3>     normals;
         std::vector<Vec2>     uvs;
         std::vector<uint32_t> indices;   // uint32_t so you can pass straight to VulkanMesh::Upload
     };
     Mesh Primitives::Cube  (float size = 1.0f)
     Mesh Primitives::Plane (float width = 1.0f, float height = 1.0f)
     Mesh Primitives::Sphere(float radius = 0.5f, int rings = 16, int sectors = 32)
     Mesh Primitives::Quad  ()
     Mesh Primitives::Line  (const Vec3& a, const Vec3& b)
   Example:
     auto cube = VCK::Primitives::Cube(1.0f);
     // vertexSize is the TOTAL byte count of the vertex buffer, not the
     // per-vertex stride - VulkanMesh::Upload passes it straight to
     // staging.CreateStaging / staging.Upload.  Multiply the element size
     // by the element count.
     mesh.Upload(device, command,
                 cube.positions.data(),
                 sizeof(VCK::Vec3) * cube.positions.size(),
                 static_cast<uint32_t>(cube.positions.size()),
                 cube.indices.data(),
                 static_cast<uint32_t>(cube.indices.size()));

*/


#include "layers/expansion/VCKExpansion.h"
#include "layers/execution/VCKExecution.h"

// =============================================================================
//  END OF VCK.h
// =============================================================================
