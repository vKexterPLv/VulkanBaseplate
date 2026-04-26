<div align="center">

# Design

Rules · status · honest caveats · roadmap

</div>

---

## Categories

The 24 rules below collapse into six categories. Every behavioural choice
VCK ships should resolve to one of these. New rules go under whichever
category they fit; no rule lives outside the index.

| # | Category | Slogan | Rules |
|---|---|---|---|
| **I** | **Explicitness** | No magic, no hidden behavior, no global state — all choices live in `cfg`. | R1, R6, R10, R23, R24 |
| **II** | **Ownership** | Core owns. Layers borrow. Strict lifecycle order. VCK destroys only what it created. | R2, R3, R5, R22 |
| **III** | **Synchronisation** | No hidden sync. Explicit fences / semaphores / timeline only. Frame is the unit of truth. External sync is the caller's responsibility. | R4, R8, R17, R18 |
| **IV** | **Cost & Scope** | Minimal core surface. No engine assumptions. Zero cost for unused features. | R15, R16, R19 |
| **V** | **Reliability** | Deterministic frame behaviour. Explicit recreation events. Fail fast, fail loud — always. | R11, R12, R14 |
| **VI** | **Transparency** | User owns the frame. Escape hatches everywhere. Debuggability is core. Every public API has an example. `VCK.h` is the surface. Extensions are logged. | R7, R9, R13, R20, R21, R23 |

R23 spans I + VI: it's both an explicitness rule (extensions are not
silent) and a transparency rule (the user can read the log to know what
their device is running). That overlap is intentional.

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

4. **No hidden synchronisation.** Only `Shutdown()` paths may call
`vkDeviceWaitIdle`; the runtime hot path never does (v0.3). The remaining
blocking sites are the concrete allow-list rule 8 expands on:
- `VulkanOneTimeCommand::End` — `vkWaitForFences` on the per-command fence,
  setup paths only. Short-circuits on submit failure (no infinite hang).
- `VulkanMemoryManager::SubmitStagingCmd` — `vkWaitForFences` on a
  per-submit `VkFence` (v0.3 replaced the old `vkQueueWaitIdle`). Release /
  acquire ownership barriers when the transfer queue is dedicated.
- `FrameScheduler::BeginFrame` / `EndFrame` — `vkWaitSemaphores` on the
  scheduler's timeline (v0.3) or, when the device doesn't expose
  `VK_KHR_timeline_semaphore`, per-slot `vkWaitForFences`.
- `FrameScheduler::DrainInFlight` — the scheduler's own full drain (used
  by the scheduler-aware `HandleLiveResize` overload instead of
  `vkDeviceWaitIdle`).
- `BackpressureGovernor::WaitIfOverrun` for `AsyncMax`.
- `VulkanSwapchain::Recreate(w, h, drainedExternally = false)` —
  `vkDeviceWaitIdle` around swapchain/framebuffer rebuild by default; the
  scheduler-aware `HandleLiveResize(window, sc, fb, pipe, scheduler)`
  overload drains via `FrameScheduler::DrainInFlight()` and passes
  `drainedExternally = true` so the global wait is skipped. Both paths
  log via `VCKLog::Notice("LiveResize", ...)` and
  `VCKLog::Notice("Swapchain", "Recreating ...")`.
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
- **Caveat:** `VCKLog` intentionally keeps a small amount of
  process-global state for debug gating (`SetDebug` / `IsDebug`) and
  consecutive-line dedup (last key + repeat count). This is logger
  convention, not GPU state — it does not track resources, device
  handles, or anything that would violate the spirit of the rule. It
  is not thread-safe in the racy-reads sense (last-writer-wins on the
  dedup counter); log output is advisory, not a source of truth.

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
- No silent fallback behaviour.
- Every failure returns an explicit `bool` / error code **and** logs
  through `VCKLog::Error` with a tag naming the subsystem. A `return
  false` without a matching `VCKLog::Error` is a bug.
- `VK_CHECK` routes non-`VK_SUCCESS` results to `VCKLog::Error`
  regardless of `cfg.debug`, so a failed Vulkan call is never silent.
- Errors propagate upward through return values; the kit never
  `std::terminate`s or throws across an API boundary.

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

18. **EXTERNAL SYNCHRONISATION**
- Every VCK instance is externally synchronised, matching the Vulkan
  spec's thread-safety rules.
- Concurrent access to the same `VulkanDevice`, `VulkanSwapchain`,
  `VulkanQueue`, `VulkanCommand`, `VulkanSync`, `FrameScheduler`, or
  `VulkanMemoryManager` from multiple threads is undefined behaviour
  unless the caller provides the lock.
- VCK never holds an internal mutex across a user-visible call. Queue
  submission is the caller's serialisation point.
- `JobGraph` is the one exception: its worker pool is internal and
  owns its own synchronisation; jobs queued via `AddJob` are scheduled
  across VCK-owned threads.

19. **ZERO COST FOR UNUSED FEATURES**
- A module that has not been `Initialize`d must allocate nothing,
  spawn no thread, emit no log line, and hold no OS handle.
- Paying for a feature requires opting in to it by name
  (`FrameScheduler`, `VulkanMemoryManager`, `TimelineSemaphore`,
  `DebugTimeline`, `cfg.enableTimeline`, …).
- `VulkanContext` + `VulkanDevice` + `VulkanSwapchain` + `VulkanSync`
  + `VulkanCommand` is the minimum viable frame loop. Anything the
  user does not touch stays inert.

20. **EVERY PUBLIC API HAS AN EXAMPLE**
- Every public class in `VCK.h` is exercised by at least one example
  under `example/`.
- New public classes (or new semantically-meaningful overloads) land
  in the same PR as an example that uses them.
- Examples double as executable documentation: `example/<Name>/App.cpp`
  header block states *what is demonstrated*, *why you'd use it*, and
  *what to look for in the console*.

21. **`VCK.h` IS THE API SURFACE**
- `VCK.h` at the repo root is the only stable include path and the
  only place documentation lives for the public surface.
- Layer headers under `layers/{core,expansion,execution,vmm}/` are
  implementation detail. Their paths and contents may change in any
  release; user code must not include them directly.
- Breaking changes to `VCK.h` bump the minor version (0.x) until
  v1.0.0. Additive changes (new class, new enum value, new overload)
  are patch or minor at the maintainer's discretion.

22. **VCK NEVER OWNS USER HANDLES**
- Rule 9 guarantees a raw escape hatch. This rule completes it:
  VCK destroys only the Vulkan handles it created.
- Any `VkBuffer`, `VkImage`, `VkCommandBuffer`, `VkSemaphore`,
  `VkFence`, `VkPipeline`, or other handle passed **in** to a VCK
  function is caller-owned. Passing it confers *use* for the duration
  of the call, never *ownership* or a destroy obligation.
- Symmetrically, handles returned by VCK getters
  (`GetSwapchain()`, `GetRenderPass()`, `GetCommandBuffer(i)`, …) are
  borrows: the user must not destroy them; they die with their VCK
  owner during the shutdown chain (rule 3).

23. **EXTENSION TRANSPARENCY**
- Any instance- or device-level extension VCK enables on the user's
  behalf must be announced via `VCKLog::Notice("<Subsystem>", ...)` at
  initialisation time, including:
  - the extension name (`VK_KHR_timeline_semaphore`,
    `VK_EXT_debug_utils`, …),
  - whether the adapter / loader actually supports it, and
  - the fallback path taken when it does not (e.g. "timeline
    semaphores unavailable — `FrameScheduler` will use per-slot
    fences").
- The user must never be surprised by what's running underneath. Every
  enabled extension is greppable in the log; every absent extension is
  greppable in the log; the choice between them is greppable in the
  log.
- Concretely: `VulkanContext::Initialize` and `VulkanDevice::Initialize`
  are the two enablement points. Both must emit one `Notice` line per
  extension they request.

24. **`cfg` IS THE CONTRACT**
- Every behavioural difference VCK can express must be reachable
  through `VCK::Config` (and its sub-structs `cfg.device`,
  `cfg.swapchain`, `cfg.pipeline`, `cfg.scheduler`, `cfg.aa`,
  `cfg.debug`, …). Nothing that changes rendering behaviour is
  hardcoded silently inside a `.cpp`.
- If it is not in `cfg`, it does not exist as a user choice — it is
  either an implementation detail (and stays out of `cfg`) or a bug
  (and must be promoted to `cfg`).
- Defaults belong on the field declaration in the struct, not in code.
  A user who never touches `cfg` gets the default behaviour; a user
  who reads the struct sees every knob the kit exposes in one place.
- Adding a runtime branch on something other than a `cfg` field
  (compile-time `#define`, environment variable, `extern bool`)
  violates this rule. The single existing exception is
  `VULKAN_VALIDATION` — a build-config toggle, not a runtime choice,
  documented in `VulkanContext.cpp`.

## Status and caveats

- **Dedicated queues (v0.3).** `VulkanDevice::FindQueueFamilies` picks a
  compute-only and a transfer-only family when the vendor exposes them.
  Fallbacks to the graphics family are logged via `VCKLog::Notice("Device",
  ...)`. Thread safety is governed by rule 18 — different `VkQueue`s are
  independent external-sync scopes, so graphics + compute + transfer
  submits from separate threads are safe.
- **Timeline semaphores (v0.3).** `VulkanDevice::Initialize` enables
  `VK_KHR_timeline_semaphore` when the adapter supports it (Vulkan 1.2+).
  `VulkanDevice::HasTimelineSemaphores()` exposes the capability.
  `FrameScheduler` automatically uses a single per-scheduler timeline for
  frame retirement; per-slot fence path remains as a fallback. Driven by
  rule 19: the timeline is only allocated when both `cfg.enableTimeline`
  and `HasTimelineSemaphores()` are true.
- **VMM staging (v0.3).** `VulkanMemoryManager::SubmitStagingCmd` no longer
  calls `vkQueueWaitIdle`; it uses a per-submit `VkFence`. When the
  transfer queue is dedicated, staging runs on the transfer family and a
  release/acquire ownership-barrier pair is recorded so the graphics queue
  sees the expected image layout (Vulkan §7.7.4). CPU-serialised for v0.3;
  a semaphore-driven async acquire is on the v0.4 roadmap.
- **Secondary command buffers (v0.3).** `VulkanCommand::AllocateSecondary /
  BeginSecondary / EndSecondary / ExecuteSecondaries` are the
  record-in-a-secondary / execute-via-`vkCmdExecuteCommands` path. The
  pool is shared with the primaries; multi-threaded allocation is the
  caller's sync responsibility (rule 18).
- **`LogVk` migration (v0.3).** Every call site in core / expansion /
  execution / vmm / example routes through `VCKLog::{Info, Notice, Warn,
  Error}` with a subsystem tag (rule 14). The old `LogVk` free function
  remains as a shim so user code written against v0.2 still compiles.
- `JobGraph` is a correct-but-simple `std::thread` + condvar scheduler. No
  fibres, no work-stealing. Drop-in replacement planned when a real
  workload demands it.
- `DebugTimeline` dumps as plain text to `VCKLog` and optionally exports
  `chrome://tracing` JSON via `DumpChromeTracing(path)` (v0.2.1). No
  in-repo graphical viewer — use `chrome://tracing` or
  `https://ui.perfetto.dev`.
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

Shipped in v0.3:

- [x] Enable `timelineSemaphore` on `VulkanDevice` and wire `FrameScheduler`
  to use timeline primitives throughout.
- [x] Real dedicated transfer / compute queues in `VulkanDevice` and
  `QueueSet`.
- [x] Async staging path in VMM — fence-per-submit, no `vkQueueWaitIdle`.
  Release/acquire ownership barriers across queue families.
- [x] Secondary command buffer support in `VulkanCommand`.
- [x] Remove runtime `vkDeviceWaitIdle` — scheduler-aware `HandleLiveResize`
  drains via `FrameScheduler::DrainInFlight()`.
- [x] `chrome://tracing` export (v0.2.1:
  `DebugTimeline::DumpChromeTracing`).

Deferred, in rough priority order:

1. Semaphore-driven async acquire in VMM staging (today's v0.3 path
   CPU-serialises the acquire after the transfer fence retires).
2. GPU-driven indirect-draw sample (compute generates `vkCmdDrawIndirect`
   commands).
3. Bundled graphical profiler viewer (Perfetto / chrome://tracing is the
   external viewer today).
4. MSVC/cl toolchain support (currently MinGW-w64 g++ only on Windows).
5. Unit-test harness (currently only CI-gated compile + manual Windows
   validation).
