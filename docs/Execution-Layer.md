<div align="center">

# Execution Layer

FrameScheduler · JobGraph · GpuSubmissionBatcher · BackpressureGovernor · TimelineSemaphore

</div>

---

Entirely optional. If you just want core VCK and want to drive `VulkanSync` +
`VulkanCommand` yourself, skip this page. Everything here composes on top of
the core and does not modify it.

---

## FrameScheduler + FramePolicy

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

| Policy       | Behaviour | Use case |
|--------------|-----------|----------|
| `Lockstep`   | CPU waits for GPU every frame. `EndFrame` blocks on the fence. | Debugging, deterministic captures. |
| `Pipelined`  | Standard double-buffering. CPU records frame N+1 while GPU executes N. Default. | Real apps. |
| `AsyncMax`   | CPU may run up to `asyncMaxLag` frames ahead. `BeginFrame` stalls the CPU only if the lag exceeds the cap. | Bursty CPU, high-throughput pipelines. |

### Frame loop

```cpp
VCK::Frame& f = sched.BeginFrame();

// 1. schedule CPU work
f.Jobs().Add("cull",     [&]{ Cull(scene, visible);     });
f.Jobs().Add("animate",  [&]{ TickAnimations(scene, dt); });
auto build = f.Jobs().Add("build-cmds",
                          [&]{ RecordDrawsToSecondaryCmd(...); });
sched.DispatchJobs();                      // blocks until CPU jobs done

// 2. record the primary cmd
vkCmdBeginRenderPass(f.PrimaryCmd(), ...);
// ... render ...
vkCmdEndRenderPass(f.PrimaryCmd());

// 3. queue GPU submits (graphics / compute / transfer) via the batcher
VCK::GpuSubmissionBatcher::SubmitInfo si;
si.waitSem   = f.ImageAvailable();
si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
si.signalSem = f.RenderFinished();
f.Submissions().QueueGraphics(f.PrimaryCmd(), si);

sched.EndFrame();   // flushes batches, hands the fence to the graphics queue
```

### What the scheduler does

- Advances an absolute monotonic frame counter (`sched.AbsoluteFrame()`).
- Waits on the slot's `VulkanSync` in-flight fence before reuse
  (Pipelined + Lockstep).
- Runs the backpressure governor (AsyncMax only).
- Resets `VulkanCommand`'s per-slot command buffer and begins recording.
- Flushes the submission batcher at `EndFrame`, handing the in-flight fence
  to the graphics queue so the next cycle can wait on it.
- Records CPU / stall spans into `DebugTimeline` if enabled.

### What the scheduler does **not** do

- `vkAcquireNextImageKHR` / `vkQueuePresentKHR` — remain the caller's problem
  (different semaphore semantics, swapchain-out-of-date handling).
- Own any core object. It borrows `VulkanDevice`, `VulkanCommand`, `VulkanSync`
  by reference.

## JobGraph

Minimal CPU task graph. `std::thread` workers + mutex + condvar + atomic
pending-deps counter. Correct baseline; swap for fibres / work-stealing later
without changing the surface.

```cpp
auto& jg = f.Jobs();

auto a = jg.Add("physics",   [&]{ StepPhysics(dt);       });
auto b = jg.Add("animation", [&]{ TickSkeletalAnims(dt); });
auto c = jg.Add("culling",   [&]{ Cull(scene, visible);  });

// Build command buffers only after simulation and culling finish
jg.Add("record-cmds", [&]{ RecordDraws(visible); }, { a, b, c });

sched.DispatchJobs();   // kicks the graph, blocks until everything done
```

- `Add` returns a `JobId` you can pass as a dependency to a later `Add`.
- `Reset()` is called automatically at `BeginFrame`.
- No global ordering — jobs with no dependency run in parallel on whichever
  workers are free.

## GpuSubmissionBatcher

Each frame the batcher collects `VkCommandBuffer` submissions into three
buckets (graphics / compute / transfer) and flushes once at `EndFrame`. One
`vkQueueSubmit` per queue per frame — no per-draw submission overhead.

```cpp
VCK::GpuSubmissionBatcher::SubmitInfo gi;
gi.waitSem   = f.ImageAvailable();
gi.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
gi.signalSem = f.RenderFinished();

f.Submissions().QueueTransfer(uploadCmd);             // e.g. staging
f.Submissions().QueueCompute (cullDispatchCmd);       // GPU culling
f.Submissions().QueueGraphics(primary, gi);           // render
// sched.EndFrame() calls FlushAll() — graphics gets the in-flight fence.
```

Flush order is Transfer → Compute → Graphics, so later stages can depend on
earlier work via their own semaphores.

## BackpressureGovernor

Active only for `FramePolicy::AsyncMax`. Tracks CPU-frame vs. GPU-frame
counters and makes `BeginFrame` wait if the CPU is more than `asyncMaxLag`
frames ahead.

```cpp
auto& gov = sched.Governor();
if (gov.Lag() > 3)
    LogVk("CPU is " + std::to_string(gov.Lag()) + " frames ahead of GPU");
```

GPU retirement is inferred opportunistically from per-slot fence status
(`vkGetFenceStatus`) in `BeginFrame`, so this is cheap and non-blocking.

### framesInFlight and asyncMaxLag

`FrameScheduler` reads the runtime `framesInFlight` from `VulkanSync`
(set via `Config::sync.framesInFlight` — see [Core API](Core-API))
and passes it into `BackpressureGovernor::Initialize`. The governor clamps
`asyncMaxLag` to `framesInFlight` with a logged warning if you asked for
more — deeper pipelining needs `VK_KHR_timeline_semaphore` (see
`TimelineSemaphore` below).

## TimelineSemaphore & DependencyToken

Thin wrapper over `VK_KHR_timeline_semaphore`:

```cpp
VCK::TimelineSemaphore uploadsDone;
if (!uploadsDone.Initialize(dev, /*initial=*/0))
{
    VCKLog::Notice("TimelineSemaphore",
                   "feature not enabled — use VulkanSync fences instead");
}

// Producer (GPU-side): submit with pSignalSemaphores = { uploadsDone.Handle() }
//                      and a VkTimelineSemaphoreSubmitInfo carrying value N.

// Consumer:
VCK::DependencyToken tok{ &uploadsDone, /*value=*/N };
tok.WaitHost(/*timeoutNs=*/1'000'000);   // CPU blocks until value >= N
```

`DependencyToken` is a `(sem*, value)` pair passed between systems. Invalid
tokens are a no-op on wait, which keeps call sites uniform whether the
producer ran or not.

> **Status (v0.3):** `VulkanDevice::Initialize` now enables
> `VK_KHR_timeline_semaphore` when the adapter supports it (Vulkan 1.2+, i.e.
> every modern desktop GPU). `VulkanDevice::HasTimelineSemaphores()` reports
> the capability. `FrameScheduler` automatically uses a single per-scheduler
> timeline for frame retirement when the feature is available and falls back
> to per-slot `VkFence` waits when it isn't. Both paths are exposed:
> `FrameScheduler::FrameTimeline()` returns the scheduler's own timeline,
> and `FrameScheduler::SlotToken(slot)` returns a `DependencyToken`
> bound to the slot's last signalled value.

## QueueSet

```cpp
auto& qs = sched.Queues();
VkQueue g = qs.Graphics();
VkQueue c = qs.Compute();    // may alias Graphics
VkQueue t = qs.Transfer();   // may alias Graphics
if (qs.HasDedicatedTransfer()) { /* real async uploads */ }
```

**Status (v0.3):** `VulkanDevice::FindQueueFamilies` now picks a dedicated
compute-only family and a dedicated transfer-only family when the vendor
exposes them (AMD / NVIDIA almost always; Intel often does not).
`QueueSet::Compute()` / `QueueSet::Transfer()` return the dedicated
`VkQueue`s; fallback to the graphics queue still happens when separate
families aren't available and is logged via `VCKLog::Notice("Device", ...)`.

Thread safety follows rule 18: different `VkQueue`s are independent
external-sync scopes, so graphics + compute + transfer submits from
separate threads are safe as long as each `VkQueue` is only touched by one
thread at a time. VMM staging (v0.3) uses `Transfer()` with release/acquire
ownership barriers against `Graphics()` when the families differ.

## DebugTimeline

Plain-text span recorder. Enable via
`FrameScheduler::Config::enableTimeline = true`. When disabled every method
is a cheap no-op (rule 19, zero cost for unused features).

```cpp
auto& tl = sched.Timeline();
tl.BeginCpuSpan("cull", f.Absolute());
Cull(scene, visible);
tl.EndCpuSpan("cull", f.Absolute());

// later:
tl.Dump();                              // plain text into VCKLog
tl.DumpChromeTracing("frame.json");     // chrome://tracing / Perfetto (v0.2.1)
```

The scheduler itself records `frame`, `jobs`, `fence-wait`, and
`backpressure` spans automatically.
