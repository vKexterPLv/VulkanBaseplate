# Changelog

All notable changes to VCK are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- **CI matrix: Linux + macOS jobs** — `.github/workflows/build.yml` now also runs on `ubuntu-latest` (apt: `libvulkan-dev libglfw3-dev vulkan-tools glslang-tools pkg-config g++`, `./build.sh A`) and `macos-latest` (brew: `vulkan-headers vulkan-loader glfw glslang molten-vk pkg-config`, `CXX=clang++ ./build.sh A`). The Windows job (build.bat [A] on MinGW + LunarG SDK) is unchanged and remains the canonical platform; Linux + macOS catch POSIX regressions in `VCK::Window` / `VCKCrossplatform` / `build.sh` that the Windows runner can't.
- **Design rule R23 — Extension transparency** — every instance- / device-level extension VCK enables on the user's behalf is announced via `VCKLog::Notice("Context", ...)` / `VCKLog::Notice("Device", ...)` at init, including the extension name, the support verdict from the driver, and the fallback path. The user can grep the init log for `ext ` and see exactly what's bound to the device.
- **Design rule R24 — `cfg` is the contract** — every behavioural difference VCK can express that the user can reasonably want to choose between lives in `cfg`. Litmus test: "If it changes how the user writes their renderer → `cfg`. If it changes how VCK works underneath → silent bundle." Codified in `docs/Design.md` together with R23 and the new six-category index (Explicitness / Ownership / Synchronisation / Cost & Scope / Reliability / Transparency).
- **Silent extension bundle (R24 silent path, R23 logged)** — `VulkanDevice::CreateLogicalDevice` now probes for and enables the following on-demand when the device advertises them, with one `Notice` line per result: `VK_KHR_synchronization2`, `VK_KHR_buffer_device_address`, `VK_EXT_memory_budget`, `VK_EXT_device_fault`, `VK_KHR_present_wait`, `VK_KHR_present_id`. No public API surface; symbols become reachable for v0.4 use sites (sync2 in `FrameScheduler`, BDA in VMM, `memory_budget` polling in `DebugTimeline`, `present_wait` / `present_id` pacing in `FrameScheduler`).
- **`cfg` extension knobs (R24 user-visible path, R23 logged)** —
  - `cfg.rendering.mode = RenderingMode::{Classic, Dynamic}` — `Dynamic` requests `VK_KHR_dynamic_rendering` and announces it; the dynamic-rendering codepath itself ships in v0.4, today the request is acknowledged with a fallback `Notice` and rendering stays Classic.
  - `cfg.device.enableBindless` — `true` requests `VK_EXT_descriptor_indexing` and announces it; bindless descriptor helpers (`DescriptorPool::AddBindlessSet`, `VulkanPipeline::EnableBindless`) ship in v0.4.
  - `cfg.swapchain.presentMode = PresentMode::FifoLatestReady` — selects `VK_PRESENT_MODE_FIFO_LATEST_READY_EXT` when `VK_EXT_present_mode_fifo_latest_ready` is advertised, otherwise falls back to `FIFO` with a `Notice` (rule 23 — never silently substitute a present mode).

### Changed
- **Example menu reordered low → high VCK assist** — `example/build.bat` and `example/build.sh` now present examples in tiers (raw core → debug + tooling → expansion → execution layer → mostly VCK) so the menu narrates the design surface from "you write everything" to "VCK does the boring parts". New numbering: `[1] RGBTriangle`, `[2] MipmapExample`, `[3] VMMExample`, `[4] SecondaryCmdExample`, `[5] DebugTimelineExample`, `[6] DebugShowcaseExample`, `[7] AAShowcaseExample`, `[8] JobGraphExample`, `[9] SubmissionBatchingExample`, `[10] TimelineExample`, `[11] SchedulerPolicyExample`, `[12] HelloExample`, `[13] EasyCubeExample`. No example code or behaviour changes — menu, dispatch, build-all, and `docs/Examples.md` reflect the new ordering. Historical `[#]` references in the v0.3.0 / v0.2.x sections below are kept as shipped at the time.
- **Cookbook expanded to 24 recipes** — added recipes 12-24 covering compute dispatch, GPU particles, indirect draw, async compute, shadow mapping, skybox / cubemap, PBR Cook-Torrance + IBL, deferred shading, HDR + tonemapping, bloom, shader hot-reload, GPU picking, and frustum culling. Cookbook now covers most rule-16 gaps (things VCK explicitly refuses to ship but every renderer ends up needing). Doc-only.
- **Linux + macOS build noise silenced** — `example/build.sh` now compiles with `-w -Werror=return-type` (matching `build.bat`'s silent-on-warnings behaviour); VMA single-header impl, GLFW Cocoa deprecations on macOS, and `vulkan_core.h` `-Wmissing-field-initializers` no longer surface as user-visible warnings. CI Linux + macOS jobs gain `apt-get -qq` / `brew install -q` flags so the workflow log isn't dominated by `Get:` / `Setting up:` / `Suggested:` lines from package install.

## [0.3.0] - 2026-04-24

v0.3 is a synchronisation-layer release. The runtime no longer calls
`vkDeviceWaitIdle` outside `Shutdown`; frame retirement is consolidated on
one per-scheduler timeline semaphore (with a fence fallback when the device
doesn't expose the feature); VMM staging gets its own transfer-family
command pool with release/acquire ownership barriers; swapchain recreate
is scheduler-aware; and secondary command buffers are supported.

### Added
- **Dedicated compute + transfer queues** — `VulkanDevice::FindQueueFamilies` now picks dedicated compute-only and transfer-only families where the vendor exposes them (AMD / NVIDIA almost always; Intel often does not). `VulkanDevice::GetComputeQueue()` / `GetTransferQueue()` return the dedicated `VkQueue`s (rule 9 escape hatch). Fallback to the graphics queue is logged via `VCKLog::Notice` when separate families aren't available. Thread safety: different `VkQueue`s = different external-sync scopes per rule 18 — callers can submit graphics + compute in parallel without locking.
- **Timeline semaphore feature enable** — `VulkanDevice::Initialize` chains `VkPhysicalDeviceTimelineSemaphoreFeatures` into `VkDeviceCreateInfo::pNext` when the adapter supports it (Vulkan 1.2+, functionally every modern GPU). `VulkanDevice::HasTimelineSemaphores()` exposes the capability to the rest of the library. `TimelineSemaphore::Initialize` now returns `true` on supporting adapters instead of silently failing.
- **Scheduler-owned timeline** — `FrameScheduler` allocates one `TimelineSemaphore` at `Initialize` (gated on `cfg.enableTimeline` **and** `device.HasTimelineSemaphores()` — rule 19, zero cost when disabled). `EndFrame` signals a monotonically increasing per-slot value (`m_SlotTimelineValue[slot] = ++m_NextTimelineValue`). `RetireCompletedFrames` uses one `vkGetSemaphoreCounterValue` call instead of N `vkGetFenceStatus` calls (O(1) device chatter). New public `FrameScheduler::FrameTimeline()` + `FrameScheduler::SlotToken(slot)` expose the timeline to external waiters.
- **`FrameScheduler::DrainInFlight()`** — waits on the scheduler's own in-flight work (timeline when active, per-slot fences otherwise) without touching `vkDeviceWaitIdle`. Replaces the global device wait on window resize.
- **Scheduler-aware `HandleLiveResize` overload** — `VCK::HandleLiveResize(window, swapchain, framebuffers, pipeline, scheduler [, depth])` drains the scheduler and recreates the swapchain without a global wait. The old `(window, device, ...)` overload remains available for scheduler-free code.
- **VMM release/acquire ownership barriers** — `VulkanStagingAllocator` records a release barrier on the transfer queue, waits on the per-submit fence, then records an acquire barrier on the graphics queue before first use. Preserves async-upload on dedicated transfer families without violating Vulkan §7.7.4 ownership rules on `VK_SHARING_MODE_EXCLUSIVE` resources. CPU-serialised for v0.3; a semaphore-driven async acquire can follow in v0.4.
- **Secondary command buffer API** — `VulkanCommand::AllocateSecondary()` / `FreeSecondary(cb)` / `BeginSecondary(cb, inheritance, extraFlags = 0)` / `EndSecondary(cb)` / static `ExecuteSecondaries(primary, cbs, count)`. `BeginSecondary` sets `VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT` by default; callers may OR in `ONE_TIME_SUBMIT` / `SIMULTANEOUS_USE` via `extraFlags`. Rule 18 (no internal lock) — callers serialise multi-threaded allocation themselves.
- **New example `[13] SecondaryCmdExample`** — records the RGB triangle's draw in a per-slot secondary, primary opens the render pass with `VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS` and dispatches it via `vkCmdExecuteCommands`. Exercises the secondary-command API plus the scheduler-aware `HandleLiveResize` overload. Rule 20.

### Changed
- **Per-submit fence in VMM staging** — `VulkanStagingAllocator::SubmitStagingCmd` no longer calls `vkQueueWaitIdle`; it creates a one-shot `VkFence`, submits with it, and `vkWaitForFences` / destroys on the CPU side (rule 4 allow-list shrinks). When the transfer queue is dedicated, staging and graphics submits overlap.
- **Frame retirement consolidated on one word** — when the timeline is active, `BeginFrame` / `EndFrame` / lockstep prefer `vkWaitSemaphores` over per-slot fence waits. Per-slot fences remain as the fallback path when the feature isn't supported.
- **`VulkanSwapchain::Recreate(w, h, drainedExternally = false)`** — opt-in flag skips the internal `vkDeviceWaitIdle` when the caller has already drained (scheduler-aware `HandleLiveResize` passes `true`). Default behaviour unchanged for existing users.
- **`VulkanDevice::Shutdown`** — now resets `m_ComputeQueue` / `m_TransferQueue` so `GetComputeQueue` / `GetTransferQueue` don't return dangling handles post-shutdown.
- **`LogVk` → `VCKLog::{Info,Notice,Warn,Error}` migration** — every call site in core / expansion / execution / vmm / example now routes through `VCKLog` with a subsystem tag. `LogVk` stays as a shim for user code that hasn't migrated yet.
- **`build.bat` / `build.sh`** — menu now `[1]`-`[13]`; `[A]` builds all 13 examples.

### Fixed
- **`VulkanOneTimeCommand::End` infinite hang** — previously called `vkWaitForFences(UINT64_MAX)` unconditionally; if `vkQueueSubmit` failed the fence would never be signalled and the call would block forever. Now short-circuits on submit failure.
- **Cross-family submit during staging** — first v0.3 iteration submitted a graphics-pool-allocated command buffer to the transfer queue (spec violation on dedicated-transfer GPUs). Fixed by giving VMM its own transfer-family command pool.
- **VMM fallback path missing `VK_CHECK`** — fence-creation-failed branch submitted without the standard `VK_CHECK` wrapper, silently dropping `VK_ERROR_OUT_OF_DEVICE_MEMORY` / friends (rule 14).
- **Stale acquire barriers on failed transfer submit** — if the transfer submit failed the CPU-side acquire barrier list would still run on the graphics queue with `oldLayout = TRANSFER_DST_OPTIMAL` on images actually still `UNDEFINED`. The acquire list is now cleared when the release submit fails.

## [0.2.1] - 2026-04-23

### Added
- **Rules 18-22** in `docs/Design.md`:
  - **R18 External synchronisation** — codifies Vulkan's per-handle external-sync requirement; `JobGraph` is the one exception.
  - **R19 Zero cost for unused features** — un-`Initialize`d modules allocate nothing, spawn no thread, emit no log line.
  - **R20 Every public API has an example** — public classes in `VCK.h` must be exercised by at least one example under `example/`.
  - **R21 `VCK.h` is the API surface** — layer headers are implementation detail; breaking changes to `VCK.h` bump the minor version until v1.0.0.
  - **R22 VCK never owns user handles** — completes rule 9; VCK destroys only handles it created.
- **`VCKMath.h`** — `Vec2` / `Vec3` / `Vec4` / `Mat4` POD structs, free-function `Translate` / `Rotate` / `Scale` / `Perspective` / `LookAt` / `Radians` / `Degrees`. No templates, no SIMD, row-major.
- **`VertexLayout`** — fluent `Add(name, VertexAttrType)` → `Binding(0)` + `Attributes()` builder; returns plain `VkVertexInput*` structs the caller hands to `VulkanModelPipeline` (rule 22, no ownership).
- **`PushConstants`** — `Declare(name, type)` cold path, `Set(name, value)` + `Apply(cb, layout, stages)` hot path. Name → offset resolved at `Declare`; no hashing / `std::any` / `std::variant` in the hot path.
- **`Primitives::Cube` / `Plane` / `Sphere` / `Quad` / `Line`** — return-by-value `Mesh { positions, normals, uvs, indices }`. Shrinks cube setup from ~40 lines of vertex tables to one call.
- **`DebugTimeline::DumpChromeTracing(path)`** — emits a Chrome-tracing JSON array you can load in `chrome://tracing` or `ui.perfetto.dev`. No viewer bundled.
- **New example `[10] DebugShowcaseExample`** — guided tour of every `VCKLog` level, `cfg.debug` gating, dedup, `VK_CHECK` fail-loud path, GPU / driver / memory / surface dump. No draw loop.
- **New example `[11] AAShowcaseExample`** — `DetectRecommendedAA` decision matrix across `forwardRenderer` × `supportsMotionVectors`, live swapchain auto-pick echoed via `GetAATechnique()` + `GetMSAASamples()`, RGB triangle drawn with the picked AA.
- **New example `[12] EasyCubeExample`** — `Primitives::Cube()` + `VertexLayout` + `PushConstants` + `VCKMath` in one screen. Rule 20 parity for the v0.2.1 ergonomic API.
- **Wiki `Cookbook`** — one-stop recipe book: image / OBJ / cube / text / line / circle / FXAA / SMAA / TAA skeletons / ImGui bootstrap / offscreen PNG readback.

### Changed
- **Rule 14 tightened** — now explicitly requires `VCKLog::Error` with a subsystem tag on every failure; a `return false` without a matching `Error` is a bug.
- **`VulkanModelPipeline::Initialize` (4-arg overload)** emits `VCKLog::Warn` when called; the hardcoded `VK_SAMPLE_COUNT_1_BIT` is hazardous when the render pass uses MSAA. Use the 5-arg overload and pass `swapchain.GetMSAASamples()`.
- **`build.bat` / `build.sh`** — menu grew to `[1]`-`[12]`.

### Fixed
- **Top-left quadrant rendering in `MipmapExample` + `VMMExample`** — caused by pipeline-vs-render-pass sample-count mismatch (pipeline was 1x, render pass was MSAA 4x — undefined per spec, on NVIDIA confines rasterisation to the top-left quadrant). Both examples now pass `swapchain.GetMSAASamples()` to the 5-arg `VulkanModelPipeline::Initialize`.
- **Windows ANSI colour output** — `VCKLog::Init()` now calls `SetConsoleMode` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING` once on startup so Windows CMD renders colours instead of `←[96m` literals.
- **`build.bat` line endings** — `.gitattributes` forces CRLF on `*.bat` so Windows CMD stops fragmenting comment lines into garbage tokens.
- **`EasyCubeExample` `sizeof` bug** — `VulkanMesh::Upload` takes total byte count, not per-vertex stride; was uploading 1 of 24 vertices.
- **`DebugTimeline::DumpChromeTracing` underflow guard** — `endUs - startUs` now matches `Dump()`'s `endUs > startUs ? endUs - startUs : 0` pattern.

## [0.2.0] - 2026-04-23

### Added
- **Cross-platform support** (Windows / Linux / macOS) via `VCK::Window` + `VCKCrossplatform` facade over GLFW. `VCK_PLATFORM_WINDOWS/LINUX/MACOS` macros gate platform includes.
- **`example/build.sh`** — Linux + macOS builder with the same `[1]-[9] / [A] / [0]` menu as `build.bat`; auto-detects OS, uses `pkg-config` for Vulkan + GLFW.
- **Live resize as first-class** — `VCK::HandleLiveResize(window, dev, sc, fb, pipe[, depth])` handles any OS resize (720p ↔ 4K) in one call per frame. Timeline-aware overloads in `layers/execution/` emit a `DebugTimeline` CPU span (rule 12).
- **Anti-aliasing framework** — `VCK::AATechnique` enum (`Auto, Off, MSAA, MSAA_A2C, SampleRate, FXAA, SMAA_1x, SMAA_T2x, TAA, TAAU`). `cfg.aa.technique = AATechnique::Auto` runs a 5-step decision tree at `VulkanSwapchain::Initialize`. Sample-based techniques implemented pipeline-side; post-process names returned via `swapchain.GetAATechnique()` for renderer to implement (rules 15/16).
- **`VCK::VCKLog`** structured logger — `Info` (debug-gated) / `Notice` / `Warn` / `Error`. Console-spam dedup of identical consecutive `(tag, body)` lines. Classify helper parses legacy `LogVk("[Tag] body")` into the new levels.
- **`cfg.debug`** flag (default `false`) — `VulkanContext::Initialize` wires it into `VCKLog::SetDebug()`.
- **`cfg.pipeline.alphaToCoverage`** — flips `alphaToCoverageEnable` in pipeline multisample state.
- **`cfg.pipeline.sampleRateShading` + `minSampleShading`** — enables per-sample shading.
- **Repo reorganisation**: `layers/{core,expansion,execution,vmm}/` + `vendor/{vulkan_headers,glfw,vma}/`. `VCK.h` at root is the single source of truth for API documentation.
- **Wiki** — `Hello-VCK.md` (per-line walkthrough targeting 1-hour onboarding budget), `_Sidebar.md` (navigation).
- **`LICENSE`**, **`CHANGELOG.md`**, **`CONTRIBUTING.md`**, **`.editorconfig`**, **`.gitignore`** at repo root.

### Changed
- **`VK_CHECK`** now routes failures directly to `VCKLog::Error(…)` instead of the debug-gated `LogVk(…)` — fail loud regardless of `cfg.debug` (rule 14 fix).
- **`VulkanContext::Initialize`** takes `VCK::Window&` (was `HWND` on Windows only); raw-handle overload preserved as escape hatch (rule 9).
- **`<windows.h>` / `<vulkan_win32.h>`** guarded behind `VCK_PLATFORM_WINDOWS` in `VulkanHelpers.h` and `VCK.h` — no more platform leakage in the public surface.
- **`docs/Design.md`** rule 4 allow-list updated to include `VulkanSwapchain::Recreate` and `HandleLiveResize` `vkDeviceWaitIdle` calls.
- **Examples 1-9** ported to `VCK::Window` + `VCK::HandleLiveResize` — dropped raw `GLFWwindow*`, `HWND`, `g_Resized`, `g_Minimized`, `OnFramebufferResize`.

### Removed
- `core/VulkanHelpers.cpp` (empty shell).
- `core/` at repo root (moved to `layers/core/`).
- `VMM/` at repo root (moved to `layers/vmm/`).
- `BuildRequiredExtensions` helper in `VulkanContext.cpp` (dead code from pre-crossplatform era, no matching header decl).

## [0.1.0] - 2026-04-22

The first release under the VCK name. Project was rebranded from VVCS
("Vulkan-VCS") to **VCK — Vulkan Core Kit** in commit `deced77`. This
release establishes the public surface (`VCK.h` amalgam header), the
three-layer architecture (core / expansion / execution), the in-tree
example builder (`build.bat` + ANSI menu), the structured logger, and
a Windows CI gate. The project is Windows-only at this point;
cross-platform support lands in v0.2.0.

### Added
- **Project rebrand** — `VVCS → VCK (Vulkan Core Kit)`. README, build
  scripts, header guards, and namespace `VCK::` finalised.
- **`VCK.h` amalgam header** — single public include surface; per-class
  declarations stop being duplicated and the amalgam now `#include`s
  the layer headers directly (rule 21 precursor).
- **Three-layer architecture** — `core/` (`VulkanContext`,
  `VulkanDevice`, `VulkanSwapchain`, `VulkanPipeline`,
  `VulkanCommand`, `VulkanSync`, `VulkanBuffer`, `VulkanImage`,
  `VulkanHelpers`), **expansion** (framebuffers, depth, samplers,
  textures, model pipelines, descriptor sets, mipmaps), **execution**
  (`FrameScheduler`, `JobGraph`, `GpuSubmissionBatcher`,
  `BackpressureGovernor`, `DebugTimeline`, `TimelineSemaphore`).
- **`VCK::Config`** master init-chain control struct — `cfg.device`,
  `cfg.swapchain`, `cfg.pipeline`, `cfg.scheduler`, `cfg.aa`, `cfg.debug`
  knobs; passed once to `VulkanContext::Initialize`.
- **`VulkanPipeline::Config`** — cull mode, front-face winding, blend
  state, push-constant ranges, descriptor-set layouts.
- **Mailbox present mode by default** with FIFO fallback when the
  surface doesn't expose Mailbox.
- **Six VCKExpansion examples** — `HelloExample`, `ModelExample`,
  `VMMExample`, `FrameSchedulerExample`, `TripleBufferExample`,
  `LockstepExample`, plus three execution-layer examples
  (`SubmissionBatchingExample`, `JobGraphExample`,
  `DebugTimelineExample`).
- **`example/build.bat`** — Windows MinGW builder with `[1]-[9] / [A] / [0]`
  ANSI-coloured menu, BUILD_ALL section that compiles every example in
  one pass, and CRLF-safe shader compile via `glslangValidator`.
- **VMM (Vulkan Memory Manager)** — three-layer allocator
  (`VmmPersistent` / `VmmTransient` / `VmmStaging`) wrapping VMA's
  pool-aware allocators. `VmmBuffer` / `VmmImage` are typed
  POD-by-handle wrappers. `LogStats` dump every N frames for the
  example.
- **`VCKLog`** — coloured `[VCK] [Tag] body` logger with `Info` /
  `Notice` / `Warn` / `Error` levels and an init-time global toggle
  for debug-gated `Info` lines.
- **`docs/`** at repo root — split out from README into
  `Build.md`, `Examples.md`, `Design.md`, `Home.md`. README slimmed to
  badges + 60-second tour + pointers.
- **GitHub wiki** — initial structure; `_Sidebar.md`, design rules.
- **CI: Windows workflow** — `.github/workflows/build.yml` runs
  `build.bat A` on `windows-latest` for every push / PR. SDK installer
  + `vk_video/*.h` sparse-checkout patch (the LunarG installer's
  `vulkan_video` component is optional but `vulkan_core.h` hard-includes
  it).
- **`.gitattributes`** — forces CRLF on `*.bat` so Windows CMD doesn't
  fragment comment lines into garbage tokens.

### Changed
- **`build.bat`** redesigned with an ANSI UI; `chcp 65001` for UTF-8;
  em-dashes scrubbed from console-bound strings (CMD prints them as `?`
  on default codepages).
- **`AllocConsole` removed** in favour of g++'s default
  console-subsystem stdout — no flicker on launch, no leftover console
  if the program crashes.
- **`VulkanContext::Initialize`** drops the leftover
  `BuildRequiredExtensions` helper (Win32-only dead code with no header
  declaration).

### Fixed
- **VMM lifecycle bugs** — wrong destruction order between
  `VmmTransient` ring slots and `VmmStaging` was leaking allocations
  on `Shutdown`. Routed VMM logs to a Windows console.
- **`FrameScheduler` Lockstep + AsyncMax deadlocks** — Lockstep was
  waiting on the slot fence before signalling the previous frame's
  release; AsyncMax could double-acquire the same slot under contention.
- **`build.bat` BUILD_ALL** trailing whitespace in `set EX=...` causing
  the all-build pass to skip examples whose name had a trailing space.
- **MinGW compile** — `SubmitInfo` and `VCK::Config` need explicit
  default ctors; gcc 13 wouldn't aggregate-init through inheritance.
- **`JobGraph::Add`** — counted invalid (out-of-range) deps toward the
  job's wait count, so `Execute()` could deadlock waiting for a job that
  would never run.
- **`VCKExpansion.h`** — forward-declared `VulkanDepthBuffer` before
  `VulkanFramebufferSet` (compile order fix).
- **`HelloExample`** — quad winding flipped so the embedded "Hello,
  World" text isn't back-face culled.

## [0.0.x] - 2026-04 (pre-rebrand prehistory)

The original project was named **VVCS** (Vulkan-VCS). These commits are
preserved in git history but never shipped under the VCK name; they're
listed here for repo archaeologists. No SemVer was applied during this
period.

- **Initial drop** — bare-bones `VulkanModule.h` + sample `main.cpp` /
  `App.cpp` / `App.h` exercising context + device + swapchain +
  pipeline.
- **VMM iteration** — first staging / persistent / transient layers;
  early lifecycle bug fixes.
- **Windows console plumbing** — `AllocConsole` route for log output
  (later replaced by g++ console subsystem in v0.1.0).
- **README iteration** — multiple README rewrites before the v0.1.0
  rebrand.
- `core/` (9 primitive classes + `VulkanHelpers`), `VCKExpansion` ([1]-[12] rendering building blocks), execution layer ([13]-[22] frame scheduling), `VMM/` (three-layer memory manager).
- 17 design rules documented in `docs/Design.md`.
- 9 example applications (`HelloExample`, `ModelExample`, `VMMExample`, `FrameSchedulerExample`, `TripleBufferExample`, `LockstepExample`, `SubmissionBatchingExample`, `JobGraphExample`, `DebugTimelineExample`).
- `example/build.bat` Windows MinGW builder with `[1]-[9] / [A] / [0]` menu.
- GitHub Actions CI (`.github/workflows/build.yml`) running `build.bat [A]` on Windows.

[Unreleased]: https://github.com/vKexterPLv/VCK/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/vKexterPLv/VCK/releases/tag/v0.2.0
[0.1.0]: https://github.com/vKexterPLv/VCK/releases/tag/v0.1.0
