# VCK — Vulkan Core Kit

A small, no-magic Vulkan kit for Windows + a frame-level execution orchestration layer on top.

VCK is **not an engine.** There's no scene graph, no material system, no update loop. You own the frame. VCK gives you the pieces and stays out of the way.

You drop one header into your project:

```cpp
#include "VCK.h"   // auto-includes VCKExpansion.h
```

…and you get the full kit: core Vulkan objects, higher-level building blocks (textures, meshes, descriptors, mipmaps), an optional memory manager, and an optional frame scheduler with CPU job graph + GPU submission batching.

---

## Table of contents

- [Layers](#layers)
- [Hello VCK](#hello-vck) — a ~60-line program
- [Core (`VCK.h`)](#core-vckh)
- [Expansion (`VCKExpansion.h`)](#expansion-vckexpansionh)
  - [VulkanOneTimeCommand](#vulkanonetimecommand)
  - [VulkanFramebufferSet](#vulkanframebufferset)
  - [VulkanDepthBuffer](#vulkandepthbuffer)
  - [VulkanSampler](#vulkansampler)
  - [VulkanTexture](#vulkantexture)
  - [VulkanMesh](#vulkanmesh)
  - [VulkanDescriptorLayoutBuilder](#vulkandescriptorlayoutbuilder)
  - [VulkanDescriptorPool](#vulkandescriptorpool)
  - [VulkanUniformSet\<T\>](#vulkanuniformsett)
  - [VulkanDescriptorAllocator](#vulkandescriptorallocator)
  - [VulkanModelPipeline](#vulkanmodelpipeline)
  - [VulkanMipmapGenerator](#vulkanmipmapgenerator)
- [Execution layer (`FrameScheduler`, jobs, policies)](#execution-layer)
  - [FrameScheduler + FramePolicy](#framescheduler--framepolicy)
  - [JobGraph](#jobgraph)
  - [GpuSubmissionBatcher](#gpusubmissionbatcher)
  - [BackpressureGovernor](#backpressuregovernor)
  - [TimelineSemaphore & DependencyToken](#timelinesemaphore--dependencytoken)
  - [QueueSet](#queueset)
  - [DebugTimeline](#debugtimeline)
- [VMM — optional memory manager](#vmm--optional-memory-manager)
- [Examples](#examples)
- [Build (Windows)](#build-windows)
- [Logging](#logging)
- [Design rules](#design-rules)
- [Status and caveats](#status-and-caveats)
- [License](#license)

---

## Layers

```
 VCK core                  VCK.h + Vulkan*.cpp
    ↓                      instance, device, swapchain, pipeline, command, sync
 VCK expansion             VCKExpansion.h / .cpp
    ↓                      textures, meshes, samplers, descriptors, mipmaps
 VCK execution             FrameScheduler, JobGraph, TimelineSemaphore, ...
    ↓                      (same VCKExpansion TU — opt in)
 VCK memory (VMM)          VMM/VulkanMemoryManager.{h,cpp}           (optional)
    ↓                      staging ring, transient pool, persistent registry
 Your renderer
```

Everything lives in `namespace VCK { ... }`. `LogVk` and `VK_CHECK` are at global scope so any TU can use them without a `using` declaration.

---

## Hello VCK

A minimal program that stands up a window, the full core stack, and clears the swapchain to a colour every frame via the `FrameScheduler`.

```cpp
// hello_vck.cpp
#include "VCK.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

int main()
{
    // 1. Window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow(800, 600, "Hello VCK", nullptr, nullptr);

    // 2. Core
    VCK::VulkanContext   ctx;
    VCK::VulkanDevice    dev;
    VCK::VulkanSwapchain sc;
    VCK::VulkanCommand   cmd;
    VCK::VulkanSync      sync;

    ctx.Initialize(/*appName=*/"hello", glfwGetWin32Window(win), GetModuleHandle(nullptr));
    dev.Initialize(ctx.GetInstance(), ctx.GetSurface());
    sc.Initialize (dev, ctx.GetSurface(), win);
    cmd.Initialize(dev);
    sync.Initialize(dev);

    // 3. Execution layer (optional; you could drive VulkanSync by hand instead)
    VCK::FrameScheduler sched;
    VCK::FrameScheduler::Config cfg;
    cfg.policy         = VCK::FramePolicy::Pipelined;
    cfg.enableTimeline = true;              // text spans in LogVk
    sched.Initialize(dev, cmd, sync, cfg);

    // 4. Frame loop
    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();

        VCK::Frame& f = sched.BeginFrame();

        // Acquire the swapchain image — VCK does not wrap this on purpose.
        uint32_t imageIndex = 0;
        vkAcquireNextImageKHR(dev.GetDevice(), sc.GetSwapchain(),
                              UINT64_MAX, f.ImageAvailable(),
                              VK_NULL_HANDLE, &imageIndex);

        // Clear via a render-pass begin → end (pipeline not needed for a clear).
        VkClearValue clear{};
        clear.color = {{0.1f, 0.2f, 0.3f, 1.0f}};
        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = sc.GetRenderPass();
        rp.framebuffer       = sc.GetFramebuffer(imageIndex);
        rp.renderArea.extent = sc.GetExtent();
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;
        vkCmdBeginRenderPass(f.PrimaryCmd(), &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass  (f.PrimaryCmd());

        // Queue the submit through the batcher.  Wait on image-available,
        // signal render-finished so present can hand off on GPU.
        VCK::GpuSubmissionBatcher::SubmitInfo si;
        si.waitSem   = f.ImageAvailable();
        si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(f.PrimaryCmd(), si);

        sched.EndFrame();   // flushes the batcher, advances VulkanSync

        // Present is still yours to do.  Store handles in locals — they are
        // by-value VkSemaphore/VkSwapchainKHR so we need their address.
        VkSemaphore    waitSem   = f.RenderFinished();   // signalled above
        VkSwapchainKHR swapchain = sc.GetSwapchain();

        VkPresentInfoKHR pr{};
        pr.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pr.waitSemaphoreCount = 1;
        pr.pWaitSemaphores    = &waitSem;
        pr.swapchainCount     = 1;
        pr.pSwapchains        = &swapchain;
        pr.pImageIndices      = &imageIndex;
        vkQueuePresentKHR(dev.GetPresentQueue(), &pr);
    }

    vkDeviceWaitIdle(dev.GetDevice());
    sched.Shutdown();
    sync.Shutdown(); cmd.Shutdown(); sc.Shutdown(); dev.Shutdown(); ctx.Shutdown();
    glfwDestroyWindow(win); glfwTerminate();
    return 0;
}
```

If you prefer the raw VCK core (no scheduler), replace steps 3 + 4 with a hand-rolled `vkWaitForFences` / `vkResetFences` / `BeginRecording` / `vkQueueSubmit` sequence — that pattern is what the three existing examples (`RGBTriangle`, `MipmapExample`, `VMMExample`) use.

---

## Core (`VCK.h`)

| Class              | Owns |
|--------------------|------|
| `VulkanContext`    | `VkInstance`, debug messenger, Win32 `VkSurfaceKHR` |
| `VulkanDevice`     | physical + logical device, VMA `VmaAllocator`, graphics + present queues |
| `VulkanSwapchain`  | `VkSwapchainKHR` + image views, default render pass, recreate path |
| `VulkanPipeline`   | graphics pipeline + pipeline layout builder |
| `VulkanCommand`    | command pool + one primary command buffer per frame slot |
| `VulkanSync`       | image-available / render-finished semaphores + in-flight fences |
| `VulkanBuffer`     | `VkBuffer` + VMA allocation, optional persistent map, `Upload()` |
| `VulkanImage`      | `VkImage` + view + VMA allocation, layout transition helper |

Global utilities:
- `LogVk(std::string)` — writes to **both** `OutputDebugStringA` (VS Output) and `stdout`.
- `VK_CHECK(expr)` — checks a `VkResult` and logs the failing call on non-success.
- `MAX_FRAMES_IN_FLIGHT` = `2`.

**Init order**
```
Context → Device → Swapchain → Pipeline → Command → Sync
```
**Shutdown order** is the reverse.

---

## Expansion (`VCKExpansion.h`)

All classes live in `namespace VCK`, borrow core objects by reference, and never take ownership of them.

### VulkanOneTimeCommand

```cpp
VCK::VulkanOneTimeCommand otc;
if (otc.Begin(dev, cmd))
{
    vkCmdCopyBuffer(otc.Cmd(), src, dst, 1, &region);
    otc.End();   // submit + vkQueueWaitIdle + free
}
```
One-shot GPU command for setup work (staging copies, layout transitions). Blocks on `vkQueueWaitIdle` — not for per-frame use.

### VulkanFramebufferSet

```cpp
VCK::VulkanFramebufferSet fbs;
fbs.Initialize(dev, sc, depth);  // one VkFramebuffer per swapchain image

// On window resize:
fbs.Recreate(dev, sc, depth);
```

### VulkanDepthBuffer

```cpp
VCK::VulkanDepthBuffer depth;
depth.Initialize(dev, sc.GetExtent());   // picks a supported D32/D24 format
// ... recreate on resize
depth.Recreate(dev, sc.GetExtent());
```

### VulkanSampler

```cpp
VCK::VulkanSampler samp;
samp.CreateLinear(dev);        // trilinear, repeat, anisotropy if supported
// or samp.CreateNearest(dev); // point sampling
VkSampler handle = samp.Get();
```

### VulkanTexture

```cpp
VCK::VulkanTexture tex;
tex.CreateFromPixels(dev, cmd,
                     pixels, width, height,
                     VK_FORMAT_R8G8B8A8_SRGB);
VkImageView view = tex.Image().GetView();
```
CPU → GPU upload with a hidden staging buffer + layout transitions.

### VulkanMesh

```cpp
struct V { float pos[3]; float uv[2]; };
V        verts[3] = { /* ... */ };
uint32_t idx[3]   = { 0, 1, 2 };

VCK::VulkanMesh mesh;
mesh.Upload(dev, cmd,
            verts, sizeof(verts),
            idx,   3);

// Inside a render pass:
mesh.RecordDraw(f.PrimaryCmd());
```

### VulkanDescriptorLayoutBuilder

```cpp
VkDescriptorSetLayout layout = VCK::VulkanDescriptorLayoutBuilder()
    .AddUniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
    .AddCombinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .Build(dev);
```

### VulkanDescriptorPool

```cpp
VCK::VulkanDescriptorPool pool;
pool.Initialize(dev,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                /*maxSets=*/VCK::MAX_FRAMES_IN_FLIGHT);

VkDescriptorSet sets[VCK::MAX_FRAMES_IN_FLIGHT];
pool.AllocateSets(dev, layout, sets);   // one set per frame slot
```

### VulkanUniformSet\<T\>

```cpp
struct SceneUbo { glm::mat4 view, proj; };

VCK::VulkanUniformSet<SceneUbo> ubo;
ubo.Initialize(dev, pool, layout, /*binding=*/0);

// Per frame:
ubo.Write(frameSlot, { view, proj });
vkCmdBindDescriptorSets(f.PrimaryCmd(),
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipelineLayout, 0, 1,
                        ubo.GetSet(frameSlot),
                        0, nullptr);
```

### VulkanDescriptorAllocator

For when one pool / one type isn't enough.

```cpp
VCK::VulkanDescriptorAllocator alloc;
alloc.Initialize(dev, /*maxSets=*/32, {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         16 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
});
VkDescriptorSet set = alloc.Allocate(layout);
```

### VulkanModelPipeline

```cpp
VCK::VulkanModelPipeline pipe;
pipe.Initialize(dev, sc.GetRenderPass(),
                "assets/model.vert.spv", "assets/model.frag.spv",
                uboLayout, samplerLayout,
                sizeof(PushConstants));

vkCmdBindPipeline(f.PrimaryCmd(),
                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                  pipe.Get());
```

### VulkanMipmapGenerator

```cpp
uint32_t mips = VCK::VulkanMipmapGenerator::MipLevels(w, h);
if (VCK::VulkanMipmapGenerator::IsFormatSupported(dev, VK_FORMAT_R8G8B8A8_SRGB))
{
    VCK::VulkanMipmapGenerator gen;
    gen.Generate(dev, cmd, image, w, h, mips);
}
```
The source image must have been created with `TRANSFER_SRC_BIT | TRANSFER_DST_BIT` usage.

---

## Execution layer

The scheduler, jobs, and sync primitives are entirely optional. If you want the raw VCK core and want to drive `VulkanSync` + `VulkanCommand` yourself, **skip this whole section** — everything below composes on top of the core, it doesn't modify it.

### FrameScheduler + FramePolicy

```cpp
VCK::FrameScheduler sched;
VCK::FrameScheduler::Config cfg;
cfg.policy         = VCK::FramePolicy::Pipelined;   // default
cfg.asyncMaxLag    = 2;                             // only for AsyncMax
cfg.enableTimeline = true;                          // DebugTimeline on
cfg.jobWorkers     = 0;                             // 0 → hardware_concurrency

sched.Initialize(dev, cmd, sync, cfg);
```

Three policies:

| Policy | Behaviour | Use case |
|--------|-----------|----------|
| `Lockstep`  | CPU waits for GPU every frame. `EndFrame()` blocks on the fence. | Debugging, deterministic captures. |
| `Pipelined` | Standard double-buffering. CPU records frame N+1 while GPU executes N. Default. | Real apps. |
| `AsyncMax`  | CPU may run up to `asyncMaxLag` frames ahead. `BeginFrame()` stalls the CPU only if the lag exceeds the cap. | Bursty CPU, high-throughput pipelines. |

Frame loop:
```cpp
VCK::Frame& f = sched.BeginFrame();

// 1. Schedule CPU work
f.Jobs().Add("cull",      [&]{ Cull(scene, frustum, visible);   });
f.Jobs().Add("animate",   [&]{ TickAnimations(scene, dt);       });
auto build = f.Jobs().Add("build-cmds",
                          [&]{ RecordDrawsToSecondaryCmd(...); },
                          { /* deps */ });
sched.DispatchJobs();   // blocks until all CPU jobs finish

// 2. Record primary cmd
vkCmdBeginRenderPass(f.PrimaryCmd(), ...);
// ... render ...
vkCmdEndRenderPass(f.PrimaryCmd());

// 3. Queue GPU submits (graphics / compute / transfer) via the batcher
VCK::GpuSubmissionBatcher::SubmitInfo si;
si.waitSem   = f.ImageAvailable();
si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
si.signalSem = f.RenderFinished();
f.Submissions().QueueGraphics(f.PrimaryCmd(), si);

sched.EndFrame();       // flushes batches, hands the fence to the graphics queue
```

What the scheduler does for you:
- Advances an **absolute** monotonic frame counter (`sched.AbsoluteFrame()`).
- Waits on the slot's `VulkanSync` in-flight fence before reuse (Pipelined + Lockstep).
- Runs the backpressure governor (AsyncMax only).
- Resets `VulkanCommand`'s per-slot command buffer and begins recording.
- Flushes the submission batcher at `EndFrame()`, handing the in-flight fence to the graphics queue so the next cycle can wait on it.
- Records CPU / stall spans into `DebugTimeline` if enabled.

What the scheduler does **not** do:
- `vkAcquireNextImageKHR` or `vkQueuePresentKHR` — those remain the caller's problem (different semaphore semantics, swapchain-out-of-date handling).
- Own any core object.

### JobGraph

Minimal CPU task graph. Not a fibers / work-stealing scheduler — `std::thread` workers + mutex + condvar + atomic pending-deps counter. Correct baseline; replace later without changing the surface.

```cpp
auto& jg = f.Jobs();

auto a = jg.Add("physics",       [&]{ StepPhysics(dt);               });
auto b = jg.Add("animation",     [&]{ TickSkeletalAnims(dt);         });
auto c = jg.Add("culling",       [&]{ Cull(scene, visible);          });

// Build command buffers only after simulation and culling are done
jg.Add("record-cmds", [&]{ RecordDraws(visible); }, { a, b, c });

sched.DispatchJobs();   // kicks the graph, blocks until everything done
```

- `Add` returns a `JobId` you can pass as a dependency to a later `Add`.
- `Reset()` is called automatically at `BeginFrame`.
- No global ordering — if two jobs have no dependency between them, they run in parallel on whichever workers are free.

### GpuSubmissionBatcher

Each frame the batcher collects `VkCommandBuffer` submissions into three buckets (graphics / compute / transfer) and flushes once at `EndFrame`. Only one `vkQueueSubmit` per queue per frame — no per-draw submission overhead.

```cpp
VCK::GpuSubmissionBatcher::SubmitInfo gi;
gi.waitSem   = f.ImageAvailable();
gi.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
gi.signalSem = f.RenderFinished();

f.Submissions().QueueTransfer(uploadCmd);                 // e.g. staging
f.Submissions().QueueCompute (cullingDispatchCmd);        // GPU culling
f.Submissions().QueueGraphics(primary, gi);               // render
// sched.EndFrame() calls FlushAll() — graphics bucket gets the in-flight fence.
```

Order of submission is Transfer → Compute → Graphics (so later stages can depend on earlier work via their own semaphores).

### BackpressureGovernor

Active only for `FramePolicy::AsyncMax`. Tracks CPU-frame vs. GPU-frame counters and makes `BeginFrame()` block if the CPU is more than `asyncMaxLag` frames ahead of the GPU.

```cpp
auto& gov = sched.Governor();
if (gov.Lag() > 3)
    LogVk("CPU is " + std::to_string(gov.Lag()) + " frames ahead of GPU");
```

GPU retirement is inferred opportunistically from per-slot fence status (`vkGetFenceStatus`) in `BeginFrame`, so this is cheap and non-blocking.

### TimelineSemaphore & DependencyToken

Thin wrapper over `VK_KHR_timeline_semaphore`:

```cpp
VCK::TimelineSemaphore uploadsDone;
if (!uploadsDone.Initialize(dev, /*initial=*/0))
{
    LogVk("timeline semas not enabled on device — use VulkanSync fences instead");
}

// Producer (GPU-side): submit with pSignalSemaphores = { uploadsDone.Handle() }
//                      and a VkTimelineSemaphoreSubmitInfo carrying value N.

// Consumer:
VCK::DependencyToken tok{ &uploadsDone, /*value=*/N };
tok.WaitHost(/*timeoutNs=*/1'000'000);   // CPU blocks until value >= N
```

`DependencyToken` is a `(sem*, value)` pair passed between systems. Invalid tokens are a no-op on wait, which keeps call sites uniform whether the producer ran or not.

> **Caveat:** VCK's current `VulkanDevice` does **not** enable the `timelineSemaphore` feature when creating the logical device, so `TimelineSemaphore::Initialize` will return `false` on most setups today. Enabling the feature bit in core is a one-line change planned for a follow-up. Until then, `FrameScheduler` uses `VulkanSync`'s binary fences and semaphores under the hood.

### QueueSet

```cpp
auto& qs = sched.Queues();
VkQueue g = qs.Graphics();
VkQueue c = qs.Compute();   // may alias Graphics
VkQueue t = qs.Transfer();  // may alias Graphics
if (qs.HasDedicatedTransfer()) { /* real async uploads */ }
```
`VulkanDevice` currently creates only a graphics queue, so `Compute()` and `Transfer()` alias it. `QueueSet` exists so call sites can be written with multi-queue intent today and pick up real parallelism when `VulkanDevice` grows dedicated queue support.

### DebugTimeline

Plain-text span recorder. Enable via `FrameScheduler::Config::enableTimeline = true`. When disabled, every method is a cheap no-op.

```cpp
auto& tl = sched.Timeline();
tl.BeginCpuSpan("cull", f.Absolute());
Cull(scene, visible);
tl.EndCpuSpan("cull", f.Absolute());

// After a few frames:
tl.Dump();   // prints chronological spans to LogVk()
```

The scheduler itself records `frame`, `jobs`, `fence-wait`, and `backpressure` spans automatically.

---

## VMM — optional memory manager

Three-layer GPU resource lifecycle. You do **not** need it to use VCK — the core works fine with raw `VulkanBuffer` / `VulkanImage`.

```
Layer 1  VmmRawAlloc          thin typed VMA wrappers
Layer 2  VmmRegistry          every allocation tagged with a Lifetime
Layer 3  VulkanMemoryManager  pools and strategies
```

Lifetimes:
- `Persistent` — freed at `Shutdown()` or explicit `FreeBuffer` / `FreeImage`.
- `TransientFrame` — auto-freed when the frame slot it was allocated in comes around again.
- `FrameBuffered` — one instance per frame slot.
- `Manual` — registry tracks it for stats only; caller frees it.

Strategies:
- **`StagingRing`** — single CPU-visible ring for batched uploads. `StageToBuffer` / `StageToImage` queue copies; `EndFrame` / `FlushStaging` submit the batch.
- **`TransientPool`** — one CPU→GPU block per frame slot, persistently mapped. Zero-cost pointer bumps. Reset on `BeginFrame`.
- **Persistent allocations** — textures, static meshes, anything that outlives a frame.

Typical frame (outside `FrameScheduler`):
```cpp
vkWaitForFences(..., sync.GetInFlightFence(slot), ...);   // MUST come first
vmm.BeginFrame(slot, absoluteFrame);

VCK::VmmBuffer dyn = vmm.AllocTransient(slot, "ui_verts",
                                        size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
dyn.Upload(data, size);

// ... record draws ...

vmm.EndFrame(slot);   // submits staging batch, resets ring
```

`vmm.LogStats()` prints a registry + ring snapshot any time you want a sanity check.

---

## Examples

Nine runnable examples, each built from the menu in `example/build.bat`.
Every example follows the same three-file layout:

```
<ExampleName>/
    main.cpp       // 4 lines — calls Run()
    App.h          // namespace + forward decls
    App.cpp        // Init / DrawFrame / Shutdown / Run
    assets/        // .glsl source + .spv output
```

### Core reference (no FrameScheduler)

These three exercise the raw `VulkanSync` / `VulkanCommand` path — use them
to understand the unscheduled lifecycle.

| # | Example          | Demonstrates |
|---|------------------|--------------|
| 1 | `RGBTriangle`    | coloured triangle, live window resize |
| 2 | `MipmapExample`  | texture upload + mipmap generation + sampling |
| 3 | `VMMExample`     | persistent triangle, transient animated quad, checkerboard texture, `LogStats()` every 120 frames |

### VCKExpansion execution layer

Every example in this group uses `FrameScheduler` and the rest of the
`VCKExpansion` execution layer.  Same triangle scene, different emphasis:

| # | Example                     | Demonstrates |
|---|-----------------------------|--------------|
| 4 | `HelloExample`              | smallest `FrameScheduler` program — `BeginFrame` / acquire / record / `QueueGraphics` / `EndFrame` / present |
| 5 | `JobGraphExample`           | CPU task graph per frame — `physics` ∥ `animate` → `build-cmds`, `audio` parallel, per-job timing logged every 60 frames |
| 6 | `SchedulerPolicyExample`    | press `1` / `2` / `3` to live-swap `Lockstep` / `Pipelined` / `AsyncMax`; window title shows current policy, average CPU frame µs logged every 120 frames |
| 7 | `SubmissionBatchingExample` | two independent cmd buffers per frame queued through `GpuSubmissionBatcher` → **one** `vkQueueSubmit` |
| 8 | `TimelineExample`           | `TimelineSemaphore::Initialize` probe + host-signal / `DependencyToken::WaitHost` round-trip (logs fallback when the device feature is not enabled) |
| 9 | `DebugTimelineExample`      | `cfg.enableTimeline = true`, user `BeginCpuSpan` / `EndCpuSpan` / `NoteStall`, `scheduler.Timeline().Dump()` every 120 frames |

The [Hello VCK](#hello-vck) snippet in the intro is the reference; `HelloExample`
is that snippet split into the standard three-file layout.

---

## Build (Windows)

### Requirements

- **Vulkan SDK** — set `VULKAN_SDK` to the install root.
- **glslangValidator** on `PATH` (ships with the SDK).
- **g++ (MinGW-w64)** on `PATH`.
- **GLFW Windows pre-compiled** from <https://www.glfw.org>.

### Dependency layout (first time)

```
example/
  build.bat
  deps/
    vk_mem_alloc.h                 (from the VMA repo)
    libglfw3.a                     (from GLFW lib-mingw-w64/)
    glfw/
      include/
        GLFW/
          glfw3.h
          glfw3native.h
```

### Build + run

```
cd example
build.bat
```

Pick an example from the menu. On success:

```
example\VMMExample\VMMExample.exe
```

Run from `cmd.exe` — the exe is console-subsystem, so `LogVk` output lands in the terminal you launched it from (and simultaneously in the VS Output window when attached).

---

## Logging

`LogVk(msg)` writes every line to **two** sinks:
1. `OutputDebugStringA` — Visual Studio / WinDbg Output pane.
2. `stdout` — the console that launched the exe.

`VK_CHECK(expr)` wraps any `VkResult`-returning call and logs the failing call on non-success. Use it everywhere:
```cpp
VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
```

No `AllocConsole` dance: `build.bat` produces a console-subsystem exe, so `stdout` is already wired to the launching terminal.

---

## Design rules

1. **Explicit > magic.** No hidden allocations, no singletons, no implicit lifetimes. Every `Initialize` has a matching `Shutdown` in a documented order.
2. **No ownership in the expansion / execution layer.** Every class borrows core objects by reference or raw pointer; never creates or destroys them.
3. **Strict lifecycle.**
   ```
   Init:     Context → Device → Swapchain → Pipeline → Command → Sync → (Scheduler / VMM)
   Shutdown: (Scheduler / VMM) → Sync → Command → Pipeline → Swapchain → Device → Context
   ```
   Expansion objects and VMM resources must be shut down **before** the core objects they reference.
4. **No hidden synchronisation.** The only place the kit blocks the CPU on the GPU is:
   - `VulkanOneTimeCommand::End()` and `VulkanMemoryManager::SubmitStagingCmd` — `vkQueueWaitIdle`, setup paths only.
   - `FrameScheduler::BeginFrame` / `EndFrame` when policy dictates (per slot fence wait).
   - `BackpressureGovernor::WaitIfOverrun` for `AsyncMax`.
   - Anything you do manually.
5. **Frame-scoped or persistent, nothing else.** Every GPU resource has a clear lifetime tag (VMM) or is owned by a class that does.

---

## Status and caveats

- `VulkanDevice` creates only a graphics queue. `QueueSet`'s compute/transfer slots alias graphics until that's plumbed through. `TimelineSemaphore::Initialize` similarly returns false on most setups because the feature bit isn't enabled yet. Both are one-line core changes planned as follow-ups — the **surface** is already in place so call sites don't need to change when they land.
- `VulkanOneTimeCommand` and `VulkanMemoryManager::SubmitStagingCmd` still use `vkQueueWaitIdle`. The staging ring gets reset on each `EndFrame` / `FlushStaging` while this is true. Replacing with a fence-per-submit (or timeline) path is the next VMM milestone.
- `JobGraph` is a correct-but-simple `std::thread` + condvar scheduler. No fibers, no work stealing. Drop-in replacement planned when a real workload demands it.
- `DebugTimeline` dumps as plain text to `LogVk`. No graphical viewer. A chrome://tracing exporter is easy to add later.
- Everything targets **Windows + GLFW + MinGW-w64 g++**. Porting to Linux or MSVC is a mechanical exercise.

---

## License

MIT.
