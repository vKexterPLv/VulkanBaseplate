# Changelog

All notable changes to VCK are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- **Rules 18-22** in `docs/Design.md`:
  - **R18 External synchronisation** — codifies Vulkan's per-handle external-sync requirement; `JobGraph` is the one exception.
  - **R19 Zero cost for unused features** — un-`Initialize`d modules allocate nothing, spawn no thread, emit no log line.
  - **R20 Every public API has an example** — public classes in `VCK.h` must be exercised by at least one example under `example/`.
  - **R21 `VCK.h` is the API surface** — layer headers are implementation detail; breaking changes to `VCK.h` bump the minor version until v1.0.0.
  - **R22 VCK never owns user handles** — completes rule 9; VCK destroys only handles it created.
- **New example [10] `DebugShowcaseExample`** — guided tour of every `VCKLog` level, `cfg.debug` gating, dedup, `VK_CHECK` fail-loud path, GPU / driver / memory / surface dump. No draw loop.
- **New example [11] `AAShowcaseExample`** — `DetectRecommendedAA` decision matrix across `forwardRenderer` × `supportsMotionVectors`, live swapchain auto-pick echoed via `GetAATechnique()` + `GetMSAASamples()`, RGB triangle drawn with the picked AA.

### Changed
- **Rule 14 tightened** — now explicitly requires `VCKLog::Error` with a subsystem tag on every failure; a `return false` without a matching `Error` is a bug.
- **`VulkanModelPipeline::Initialize` (4-arg overload)** emits `VCKLog::Warn` when called; the hardcoded `VK_SAMPLE_COUNT_1_BIT` is hazardous when the render pass uses MSAA. Use the 5-arg overload and pass `swapchain.GetMSAASamples()`.
- **`build.bat` / `build.sh`** — menu now `[1]`-`[11]`, `[A]` builds all 11 examples.

### Fixed
- **Top-left quadrant rendering in `MipmapExample` + `VMMExample`** — caused by pipeline-vs-render-pass sample-count mismatch (pipeline was 1x, render pass was MSAA 4x — undefined per spec, on NVIDIA confines rasterisation to the top-left quadrant). Both examples now pass `swapchain.GetMSAASamples()` to the 5-arg `VulkanModelPipeline::Initialize`.
- **Windows ANSI colour output** — `VCKLog::Init()` now calls `SetConsoleMode` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING` once on startup so Windows CMD renders colours instead of `←[96m` literals.
- **`build.bat` line endings** — `.gitattributes` forces CRLF on `*.bat` so Windows CMD stops fragmenting comment lines into garbage tokens.

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

### Added
- Initial VCK release.
- `core/` (9 primitive classes + `VulkanHelpers`), `VCKExpansion` ([1]-[12] rendering building blocks), execution layer ([13]-[22] frame scheduling), `VMM/` (three-layer memory manager).
- 17 design rules documented in `docs/Design.md`.
- 9 example applications (`HelloExample`, `ModelExample`, `VMMExample`, `FrameSchedulerExample`, `TripleBufferExample`, `LockstepExample`, `SubmissionBatchingExample`, `JobGraphExample`, `DebugTimelineExample`).
- `example/build.bat` Windows MinGW builder with `[1]-[9] / [A] / [0]` menu.
- GitHub Actions CI (`.github/workflows/build.yml`) running `build.bat [A]` on Windows.

[Unreleased]: https://github.com/vKexterPLv/VCK/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/vKexterPLv/VCK/releases/tag/v0.2.0
[0.1.0]: https://github.com/vKexterPLv/VCK/releases/tag/v0.1.0
