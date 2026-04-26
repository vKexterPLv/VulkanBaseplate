<div align="center">

# Overview &nbsp;·&nbsp; What VCK is, gives, and refuses to do

A single page to hand a new teammate, a reviewer, or yourself six months from now.

</div>

---

## TL;DR

**VCK is a small, honest, zero-magic Vulkan layer you *opt into*.** It is not a
framework, not an engine, and not a renderer. You still own the frame; VCK just
removes the 2,000 lines of boilerplate between you and `vkCmdDraw`.

One include (`#include "VCK.h"`) gets you:

- Core Vulkan objects with matched `Initialize` / `Shutdown` pairs.
- An optional expansion layer for the things you would otherwise write yourself
  (framebuffers, depth, samplers, textures, meshes, descriptors, mipmaps, AA,
  ergonomic shader inputs).
- An optional execution layer that owns the per-frame dance
  (`FrameScheduler` + `JobGraph` + submission batching + timeline semaphores).
- An optional memory manager (`VMM`) with persistent / transient / frame-buffered
  lifetimes and a pooled staging ring.

Every optional piece is *separately opt-in* and costs nothing if you don't
call `Initialize` on it. Rule 19.

---

## What VCK gives you

### Core (`layers/core/`, always on)

| Class                  | What it does |
|------------------------|--------------|
| `VCK::Window`          | Cross-platform window + input + live-resize signal; GLFW on Win/Linux/macOS. |
| `VCK::VulkanContext`   | Instance, validation layers, surface, dispatch. |
| `VCK::VulkanDevice`    | Physical + logical device, queue families, feature enables. v0.3: dedicated compute / transfer queues + timeline-semaphore feature chain. |
| `VCK::VulkanSwapchain` | Surface swapchain, present mode, MSAA selection, `Recreate(drainedExternally)`. |
| `VCK::VulkanPipeline`  | Render pass + graphics pipeline + layout. |
| `VCK::VulkanCommand`   | Per-slot primary cmd buffers + v0.3 secondary API. |
| `VCK::VulkanSync`      | Per-slot binary fences, image-available / render-finished semaphores. |
| `VCK::VulkanBuffer`    | Staging / vertex / index / uniform buffers via VMA. |
| `VCK::VulkanImage`     | 2D images with layout-transition helpers. |

That's the "I want raw Vulkan with the boilerplate gone" mode. Nothing else
needs to be initialised.

### Expansion (`layers/expansion/`, opt-in)

| Class                              | What it replaces |
|------------------------------------|------------------|
| `VulkanFramebufferSet`             | Manual `VkFramebuffer` per swapchain image. |
| `VulkanDepthBuffer`                | Manual depth image + view + format pick. |
| `VulkanSampler`                    | `VkSamplerCreateInfo` + sensible defaults. |
| `VulkanTexture`                    | Image + view + sampler + one-shot upload. |
| `VulkanMesh`                       | Vertex + index buffer + staged upload + `RecordDraw`. |
| `VulkanDescriptorPool / Set / Allocator` | Descriptor boilerplate. |
| `VulkanModelPipeline`              | Convenience front-end over `VulkanPipeline`. |
| `VulkanMipmap`                     | `vkCmdBlitImage`-based mip chain. |
| `VulkanUniformSet<T>`              | Typed per-frame uniform buffer. |
| `VertexLayout` / `PushConstants`   | Name-based attribute + push-constant declaration (v0.2.1). |
| `Primitives::Cube/Plane/Sphere/Quad/Line` | Cube setup goes from ~40 lines to one call (v0.2.1). |
| `VCKMath` (Vec2/3/4, Mat4)         | Just enough maths to feed GLSL — no SIMD, no templates (v0.2.1). |
| `HandleLiveResize`                 | Drop-in "WM_SIZE, please just work" one-liner; v0.3 scheduler-aware overload. |
| `PickMSAA` / `DetectRecommendedAA` | VRAM-tier + forward/motion-aware AA picker. |

### Execution (`layers/execution/`, opt-in)

| Class                   | What it gives you |
|-------------------------|-------------------|
| `FrameScheduler`        | Owns the per-frame loop: acquire → record → submit → present. v0.3 owns a `TimelineSemaphore`; `BeginFrame` waits on `timeline ≥ slotValue` with one `vkWaitSemaphores`. Falls back to per-slot fences when the device doesn't expose `VK_KHR_timeline_semaphore`. |
| `JobGraph`              | CPU task graph per frame — the only thread-safe VCK object (rule 18 exception). |
| `GpuSubmissionBatcher`  | `QueueGraphics` / `QueueCompute` / `QueueTransfer` → one `vkQueueSubmit` per queue per frame. |
| `BackpressureGovernor`  | Enforces `asyncMaxLag` so `AsyncMax` doesn't run the CPU away from the GPU. |
| `TimelineSemaphore` / `DependencyToken` | User-facing timeline primitives for cross-system waits. |
| `QueueSet`              | `Graphics()` / `Compute()` / `Transfer()` `VkQueue`s — really dedicated on AMD/NVIDIA in v0.3. |
| `DebugTimeline`         | Plain-text + chrome://tracing JSON span recorder. Free when disabled. |
| `FramePolicy`           | `Lockstep` / `Pipelined` / `AsyncMax`. |
| `DrainInFlight()` (v0.3) | Wait on every slot's most recent submit without touching the device globally. Replaces `vkDeviceWaitIdle` on the resize path. |

### VMM (`layers/vmm/`, opt-in)

Typed lifetimes (persistent / transient / frame-buffered), pooled staging
ring, v0.3 per-submit fence (no `vkQueueWaitIdle`), release/acquire ownership
barriers when the transfer family differs from graphics (Vulkan §7.7.4).

---

## What VCK never does

**Explicitly, by design:**

- **No engine.** No scene graph, no material system, no renderer, no asset
  pipeline, no ECS, no resource manager. Rule 16.
- **No ownership of your handles.** Any raw `Vk*` you pass into VCK stays
  yours; VCK only destroys handles it created. Rule 22.
- **No hidden globals.** No singletons, no thread-local state, no heartbeat
  thread. The only process-wide thing is the `VCKLog` dedup map — logger
  convention, not GPU state. Rule 10.
- **No hidden synchronisation.** Only `Shutdown()` paths may call
  `vkDeviceWaitIdle`. Every blocking call on the hot path is named, logged,
  and on the rule 4 allow-list (`OneTimeCommand::End`, VMM per-submit fence,
  `FrameScheduler::WaitInFlightFence`, `DrainInFlight`,
  `BackpressureGovernor::WaitIfOverrun`). Rule 4.
- **No hidden cost for unused features.** Un-`Initialize`'d modules allocate
  nothing, spawn no thread, emit no log line. `cfg.enableTimelineSemaphores =
  false` means zero timeline overhead. Rule 19.
- **No stability promises without review.** Breaking changes to `VCK.h` bump
  the minor version (`0.x`) until v1.0. Layer headers are implementation
  detail. Rule 21.

**Things VCK also deliberately does not ship** (and how you add them):

- Image / OBJ / font / audio loaders — [Cookbook](Cookbook).
- Post-process AA passes (FXAA / SMAA / TAA) — detection + sample-based AA
  are in, post-process passes are recipe code in your app — [Cookbook](Cookbook).
- ImGui / debug UI — [Cookbook](Cookbook).
- Compute shaders as a library — Vulkan pipeline objects are already exposed;
  dedicate a queue via `QueueSet::Compute()` and submit through
  `GpuSubmissionBatcher::QueueCompute`.

---

## What you can build with it

Paired, honest answer per layer:

| Layer set used                                    | You get |
|---------------------------------------------------|---------|
| Core only                                         | A raw Vulkan app with less boilerplate. You own the frame, sync, resize. |
| Core + expansion                                  | A single-pass renderer with textures, meshes, MSAA, mipmaps, descriptors, live resize. 9 of 13 examples. |
| Core + expansion + execution                      | A multi-queue, multi-submit renderer with CPU task-graph parallelism, backpressure, timeline sync, chrome://tracing capture. 4 of 13 examples. |
| Core + expansion + execution + VMM                | Everything above plus typed GPU-memory lifetimes and a pooled staging ring. `VMMExample`. |
| Core + expansion + execution + VMM + [Cookbook](Cookbook) | A full indie renderer: image loading, model loading, SDF primitives, text, FXAA / SMAA / TAA skeletons, ImGui overlay, PNG readback for golden-image tests. |

"Full indie renderer" here means: the same shape as a personal / student /
demoscene Vulkan renderer. **Not** the shape of a commercial engine — there
is no editor, no asset cooker, no cross-platform input abstraction beyond
GLFW, no audio, no animation, no physics. Rule 16 on purpose.

---

## Modern optimisations (v0.3 state)

These are the techniques VCK actually uses today, not a wishlist.

- **Timeline semaphores** (`VK_KHR_timeline_semaphore`). `FrameScheduler`
  owns one timeline, signals a monotonic per-slot value at `EndFrame`, waits
  with one `vkWaitSemaphores` at `BeginFrame`. Replaces N per-slot fence
  waits with O(1) host retirement. Zero-cost fallback to per-slot fences on
  devices that don't expose the feature (rule 19).
- **Dedicated compute + transfer queues.** `VulkanDevice::FindQueueFamilies`
  picks compute-only and transfer-only families where the vendor exposes
  them (AMD / NVIDIA almost always). Thread-safe by rule 18 (different
  `VkQueue`s = different external-sync scopes).
- **Release / acquire ownership barriers across queue families**
  (Vulkan §7.7.4). When VMM staging submits to a dedicated transfer queue,
  it records a release barrier on the transfer queue, waits on the per-submit
  fence, then records an acquire barrier on the graphics queue so
  `oldLayout` matches the post-release state.
- **Per-submit fences, not `vkQueueWaitIdle`.** VMM staging, `OneTimeCommand`,
  and the scheduler all use fine-grained fence waits. The rule 4 allow-list
  explicitly names every blocking site.
- **Scheduler-aware drain** on live resize. `HandleLiveResize(..., scheduler)`
  calls `FrameScheduler::DrainInFlight()` instead of `vkDeviceWaitIdle`, so
  work on independent compute / transfer queues keeps progressing during a
  resize. Swapchain recreation passes `drainedExternally=true` so the
  internal `vkDeviceWaitIdle` fallback is skipped.
- **Secondary command buffers.** `VulkanCommand::AllocateSecondary /
  BeginSecondary / EndSecondary / ExecuteSecondaries` for multi-threaded
  record-then-execute patterns; caller serialises pool access per rule 18.
- **Submission batching.** `GpuSubmissionBatcher` groups N command buffers
  per queue per frame into a single `vkQueueSubmit`. One syscall, not N.
- **Pipelined / AsyncMax policies.** Work can overlap by up to `framesInFlight`
  (Pipelined) or `asyncMaxLag` (AsyncMax), clamped and logged.
- **CPU parallelism via `JobGraph`.** Per-frame task graph with explicit
  dependencies; physics ∥ animate → build-cmds while audio runs in parallel.
  Thread-safe by design — the only rule 18 exception.
- **`DebugTimeline` + chrome://tracing export.** Enable via
  `cfg.enableTimeline = true`; zero-cost when off. `DumpChromeTracing(path)`
  writes a JSON file loadable in `chrome://tracing` or `ui.perfetto.dev`.
- **Structured logging with dedup.** `VCKLog` suppresses repeated error
  storms, surfaces `Info` only when `cfg.debug = true`, and routes every
  `VK_CHECK` failure to `Error` regardless of debug flag.
- **Auto-AA picker.** `cfg.aa.technique = Auto` runs a 5-step decision tree
  (VRAM tier → forward path → motion vectors → pick) once at
  `Swapchain::Initialize`. Sample-based AA is applied; post-process names
  are returned to the renderer to wire up.

---

## Why it's fresh

- **One header you can actually read.** `VCK.h` is ~1,100 lines including the
  class index, config knobs, short-form rules, and function indexes per
  `.cpp`. It *is* the documentation.
- **22 audited design rules** (see [Design](Design)). Every PR is reviewed
  against them; rule violations in review are fixed in the same PR, not
  deferred.
- **Rule 20: every public class has at least one example.** 13 examples in
  `example/` as of v0.3 cover the full surface.
- **Rule 14: every failure is a bool *and* a `VCKLog::Error`.** Silent
  failures don't exist.
- **Rule 21: breaking changes to `VCK.h` bump the minor until v1.0.** You
  know exactly what to audit when you pull.
- **Opt-in by layer, opt-in by knob.** Don't want timeline semaphores? Set
  `cfg.device.enableTimelineSemaphores = false`. Don't want a dedicated
  compute queue? Set `cfg.device.enableDedicatedComputeQueue = false`. They
  cost nothing disabled (rule 19) and the scheduler gracefully falls back.
- **No "framework tax".** VCK does not take over `main()`. It has no event
  loop. It has no lifecycle of its own. You can call `vkQueueSubmit` on a
  VCK-borrowed queue and VCK doesn't care.

---

## Honest summary (from the maintainer's side)

VCK is not trying to be Unreal, not trying to be bgfx, not trying to be
sokol. It sits in a specific spot: *"I want to write a Vulkan renderer, not a
Vulkan tutorial."* If that's you, you will feel at home. If you want a scene
graph, a material editor, or a render-graph library, this is not that tool
and the rules explicitly promise it never will be.

**Pick VCK if:**

- You are writing a small-to-medium renderer and want to skip ~2,000 lines
  of boilerplate without losing control.
- You want the per-frame timeline made legible (chrome://tracing, `VCKLog`,
  explicit fence / timeline / semaphore names).
- You want live resize and multi-queue parallelism handled correctly without
  writing the spec yourself.
- You want the public surface small enough to keep in your head.

**Pick something else if:**

- You want a batteries-included 3D engine with a scene graph.
- You want an "it just works" drop-in — VCK is explicit about *everything*.
  That's the whole point, but it does mean you still write the draw loop.
- You want a render-graph library. VCK's execution layer is a scheduler, not
  a graph compiler; if you need automatic resource aliasing and pass
  culling, you want something like `render-graph` on top of VCK, not VCK
  itself.

v0.3 puts VCK on the "production-ready for indie renderers" ladder: the last
runtime `vkDeviceWaitIdle` is gone, timeline semaphores are wired end-to-end,
dedicated queues actually parallelise, VMM is honest about fences, secondary
command buffers are usable for multi-threaded recording, and every rule 4
blocking site is on an explicit allow-list. The known unshipped items live
in [docs/Design.md → Roadmap](Design.md#roadmap) — async acquire in VMM,
GPU-driven indirect draws, bundled chrome://tracing viewer, cl/MSVC build,
unit tests.

If you're picking the kit up for the first time, read
[Hello VCK](Hello-VCK) next, then skim `VCK.h`'s header block.
