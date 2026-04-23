<div align="center">

# Design

Rules · status · honest caveats · roadmap

</div>

---

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
- `VulkanSwapchain::Recreate` and `VCK::HandleLiveResize`
  — `vkDeviceWaitIdle` around swapchain/framebuffer rebuild, always
  logged (`[LiveResize]` / `[Swapchain] Recreating`).
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
- Supported platforms: **Windows** (MinGW-w64 g++), **Linux**, **macOS**
  (latter two via `VCK::Window` + `example/build.sh`, pkg-config
  `vulkan`/`glfw3`). MSVC/cl is not wired today — mechanical port.

## Done (previously on the roadmap)

- **MSAA end-to-end.** `cfg.swapchain.msaaSamples > 1` works end-to-end:
  `VulkanSwapchain` owns the per-image multisampled `VkImage`+view,
  `VulkanPipeline` configures the render pass with a resolve attachment,
  `VulkanFramebufferSet` binds `[msaaView, swapchainView]`, recreate on
  resize rebuilds both. Zero-config unchanged. `MSAA_AUTO` sentinel +
  `DetectRecommendedMSAA` pick a sensible sample count on first
  `Initialize` (integrated → 1x, mid discrete → 4x, high discrete → 8x,
  clamped to `framebufferColorSampleCounts` ∩ depth counts).
- **AA auto-detection (first `Initialize`).** `cfg.aa.technique =
  AATechnique::Auto` runs `DetectRecommendedAA(device, forwardRenderer,
  motionVectors)` — 5-step decision tree (VRAM tier → renderer path →
  motion-vector support) picks MSAA / MSAA_A2C / SampleRate / FXAA /
  SMAA_1x / SMAA_T2x / TAA / TAAU. Sample-based techniques are wired by
  VCK end-to-end; post-process techniques are name-only and the renderer
  implements the shader (rule 15/16 — `VCK` is not a renderer).
  `VulkanSwapchain::GetAATechnique()` returns the resolved pick.
- **Logger polish.** `VCKLog` gained an explicit `Level` enum (`Info`
  debug-gated, `Notice`/`Warn`/`Error` always visible), a consecutive-line
  dedup that prints `(repeated N more times)` on the next distinct line,
  and a `SetDebug(bool)` knob tied to `cfg.debug`. Legacy `LogVk("[Tag]
  msg")` sites auto-parse the `[Tag]` prefix into `VCKLog::Info(tag, body)`
  so no mass call-site churn was needed.
- **Cross-platform facade.** `VCK::Window` + `VCKCrossplatform.{h,cpp}`
  cover Windows/Linux/macOS; all 9 examples use it, no raw GLFW/HWND in
  user code. `example/build.sh` mirrors `build.bat` on Linux/macOS.
- **Live resize as a first-class feature.** `VCK::HandleLiveResize()`
  one-call-per-frame, auto-tracks size, logs `[LiveResize]` spans.

## Anti-aliasing — scope + detector

VCK splits AA into two families and implements exactly one of them:

| Family        | Techniques                                        | Who implements? |
|---------------|---------------------------------------------------|-----------------|
| Sample-based  | `MSAA`, `MSAA_A2C`, `SampleRate`                  | **VCK**         |
| Post-process  | `FXAA`, `SMAA_1x`, `SMAA_T2x`, `TAA`, `TAAU`      | **Caller**      |

Sample-based techniques map directly to `VkPipelineMultisampleStateCreateInfo`
fields (`rasterizationSamples`, `alphaToCoverageEnable`,
`sampleShadingEnable` + `minSampleShading`). Post-process techniques are
render-pass features that belong to a renderer — shipping them would grow
the core beyond rules 15/16. VCK picks the technique name, exposes it
via `VulkanSwapchain::GetAATechnique()`, and the application's own
post-process pass implements the shader.

Vendor-specific techniques (CSAA/EQAA/TXAA/MFAA) are intentionally not
offered — they require vendor extensions that break the cross-platform
promise. Any app that needs one should pin the samples and enable the
extension directly; VCK's escape hatch (rule 9) is right there.

### The 5-step detector

`DetectRecommendedAA(VkPhysicalDevice, forwardRenderer, motionVectors)`:

1. **Query hardware** — `VkPhysicalDeviceProperties` +
   `VkPhysicalDeviceMemoryProperties` (device-local heap sum).
2. **Classify tier** — LOW (integrated OR ≤ 2 GB), MID (≤ 6 GB),
   HIGH (> 6 GB).
3. **Detect renderer path** — caller-supplied (`forwardRenderer`);
   deferred pipelines can't use MSAA efficiently.
4. **Select method**:
   - LOW → `FXAA`
   - MID + forward → `MSAA_A2C`
   - MID + deferred → `SMAA_T2x` (if motion vectors) else `SMAA_1x`
   - HIGH + forward → `TAA` (if motion vectors) else `MSAA_A2C`
   - HIGH + deferred → `TAA` (if motion vectors) else `SMAA_1x`
5. **Clamp MSAA samples** — `DetectRecommendedMSAA` picks an actual
   sample count (1/2/4/8) clamped to both
   `framebufferColorSampleCounts` AND `framebufferDepthSampleCounts`.

Every decision logs a `VCKLog::Notice("AA", ...)` line so the user sees
what got picked without enabling debug.

## Roadmap

Deferred features, in rough priority order:

1. Enable `timelineSemaphore` on `VulkanDevice` and wire `FrameScheduler`
   to use timeline primitives throughout.
2. Real dedicated transfer / compute queues in `VulkanDevice` and
   `QueueSet`.
3. Async staging path in VMM — fence-per-submit, no `vkQueueWaitIdle`.
4. GPU-driven indirect-draw sample (compute generates `vkCmdDrawIndirect`
   commands).
5. Graphical profiler — chrome://tracing export, then a bundled viewer.
6. MSVC/cl toolchain support (currently MinGW-w64 g++ only on Windows).
