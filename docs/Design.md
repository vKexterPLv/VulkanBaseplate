# Design

## Rules (strict)

1. **Explicit > magic.** No hidden allocations, no singletons, no implicit
   lifetimes. Every `Initialize` has a matching `Shutdown` in a documented
   order.
2. **No ownership in the expansion / execution layer.** Every class borrows
   core objects by reference or raw pointer; never creates or destroys them.
3. **Strict lifecycle.**
   ```
   Init:     Context → Device → Swapchain → Pipeline → Command → Sync → (Scheduler / VMM)
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
