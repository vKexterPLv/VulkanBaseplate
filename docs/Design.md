<div align="center">

# Design

Rules · status · honest caveats · roadmap

</div>

---

## Rules (strict)

## Rules (strict)

1. **Explicit > magic.** No hidden allocations, no singletons, no implicit
   lifetimes. Every `Initialize` has a matching `Shutdown` in a documented
   order.

2. **No ownership in the expansion / execution layer.** Every class borrows
   core objects by reference or raw pointer; never creates or destroys them.

3. **Strict lifecycle.**

```
Init: Context → Device → Swapchain → Pipeline → Command → Sync → (Scheduler / VMM)
Shutdown: (Scheduler / VMM) → Sync → Command → Pipeline → Swapchain → Device → Context
```

Expansion objects and VMM resources must be shut down **before** the core
objects they reference.

4. **No hidden synchronisation.** The only places the kit blocks the CPU on
the GPU are:
- `VulkanOneTimeCommand::End` and `VulkanMemoryManager::SubmitStagingCmd`
  — `vkQueueWaitIdle`, setup paths only.
- `FrameScheduler::BeginFrame` / `EndFrame` when policy dictates (per
  slot fence wait).
- `BackpressureGovernor::WaitIfOverrun` for `AsyncMax`.
- Anything you do manually.

5. **Frame-scoped or persistent, nothing else.** Every GPU resource has a
clear lifetime tag (VMM) or is owned by a class that does.

6. **NO HIDDEN BEHAVIOR**
- Nothing happens “automatically” without being explicitly traceable.
- Resize, recreation, batching, sync must be visible in logs/timeline.

7. **USER OWNS THE FRAME (UNLESS OPT-IN SCHEDULER)**
- Core mode: user controls frame loop.
- Scheduler mode: explicit opt-in only.
- Never silently take control of execution.

8. **EXPLICIT SYNCHRONIZATION MODEL**
- Only fences / semaphores / timeline tokens define ordering.
- No implicit ordering between commands, queues, or systems.

9. **ESCAPE HATCH ALWAYS EXISTS**
- Any abstraction must allow bypass:
- raw command buffer submission
- raw queue submit
- manual synchronization
- Nothing is “forced architecture”.

10. **ZERO HIDDEN STATE**
- No global GPU state tracking.
- No singleton device/context managers.
- All state is passed explicitly or owned by user-visible objects.

11. **DETERMINISTIC FRAME BEHAVIOR**
- Same inputs → same submission order and execution graph.
- Any nondeterminism must be explicitly opt-in (Async policies only).

12. **EXPLICIT RECREATION EVENTS**
- Swapchain/device/resource recreation must:
- be triggered
- be logged
- be observable in DebugTimeline
- never happen silently

13. **DEBUGGABILITY IS CORE FEATURE**
- Every abstraction must be inspectable:
- LogVk output
- DebugTimeline spans
- explicit dumps of state (queues, frames, resources)

14. **FAIL FAST, FAIL LOUD**
- No silent fallback behavior.
- Errors must:
- return explicit result
- log cause
- propagate upward

15. **MINIMAL CORE SURFACE**
- Core stays small and stable.
- Complexity belongs in:
Expansion / Execution / VMM / Tools layers.
- Core must never grow into engine logic.

16. **NO ENGINE ASSUMPTIONS**
- VCK does not assume:
- scene graph
- asset pipeline
- ECS
- renderer architecture
- It only provides execution + GPU control primitives.

17. **FRAME IS THE UNIT OF TRUTH**
- All GPU work is bound to:
- frame slot
- or explicit timeline value
- Nothing exists “outside frame context” unless explicitly persistent.

## Status and caveats

- `VulkanDevice` creates only a graphics queue. `QueueSet`'s compute /
  transfer slots alias graphics until that's plumbed through.
  `TimelineSemaphore::Initialize` similarly returns `false` on most setups
  because the feature bit isn't enabled yet. Both are one-line core changes
  planned as follow-ups — the **surface** is already in place so call sites
  don't need to change when they land.
- `VulkanOneTimeCommand` and `VulkanMemoryManager::SubmitStagingCmd` still
  use `vkQueueWaitIdle`. The staging ring gets reset on each
  `EndFrame` / `FlushStaging` while this is true. Replacing with a
  fence-per-submit (or timeline) path is the next VMM milestone.
- `JobGraph` is a correct-but-simple `std::thread` + condvar scheduler. No
  fibres, no work-stealing. Drop-in replacement planned when a real
  workload demands it.
- `DebugTimeline` dumps as plain text to `LogVk`. No graphical viewer. A
  chrome://tracing exporter is easy to add later.
- Everything targets **Windows + GLFW + MinGW-w64 g++**. Porting to Linux
  or MSVC is a mechanical exercise.

## Roadmap

Deferred features, in rough priority order:

1. **MSAA end-to-end.** `cfg.swapchain.msaaSamples` is a reserved field
   today — `VulkanPipeline` clamps `samples` to `VK_SAMPLE_COUNT_1_BIT`
   with a `LogVk` warning. Full support needs:
   - Render pass with a multisampled colour attachment + a single-sample
     resolve attachment; `subpass.pResolveAttachments` pointed at it.
   - A per-swapchain-image multisampled `VkImage` + view owned by
     `VulkanSwapchain` (or a new helper).
   - Framebuffers bind `[msaaView, swapchainView]` instead of
     `[swapchainView]` alone.
   - Recreate path on `WM_SIZE`.
   The `Config` surface is already stable; when this lands, call sites
   that already set `msaaSamples` will just start working.
2. Enable `timelineSemaphore` on `VulkanDevice` and wire `FrameScheduler`
   to use timeline primitives throughout.
3. Real dedicated transfer / compute queues in `VulkanDevice` and
   `QueueSet`.
4. Async staging path in VMM — fence-per-submit, no `vkQueueWaitIdle`.
5. GPU-driven indirect-draw sample (compute generates `vkCmdDrawIndirect`
   commands).
6. Graphical profiler — chrome://tracing export, then a bundled viewer.
