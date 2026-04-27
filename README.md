<div align="center">

# V C K

**Vulkan Core Kit** — a small, no-magic Vulkan kit for Windows, Linux, and
macOS, plus a frame-level execution orchestration layer on top.

<sub>**not** an engine · no scene graph · no material system · you own the frame</sub>

[![Windows build](https://github.com/vKexterPLv/VCK/actions/workflows/build.yml/badge.svg?branch=VCK)](https://github.com/vKexterPLv/VCK/actions/workflows/build.yml)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)](docs/Build.md)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](CONTRIBUTING.md)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red)](https://www.vulkan.org/)
[![Docs](https://img.shields.io/badge/docs-wiki-blue)](https://github.com/vKexterPLv/VCK/wiki)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![Changelog](https://img.shields.io/badge/changelog-keep%20a%20changelog-orange)](CHANGELOG.md)

</div>

---

Drop one header:

```cpp
#include "VCK.h"   // auto-includes the expansion + execution layers
```

…and you get the full kit: core Vulkan objects, higher-level building blocks
(textures, meshes, descriptors, mipmaps), an optional memory manager (VMM),
and an optional frame scheduler with a CPU job graph + GPU submission
batcher.

**What's in the box (v0.3):**

- **Cross-platform**: Windows / Linux / macOS via `VCK::Window` + `VCK_PLATFORM_*` macros — no raw GLFW or Win32 in user code.
- **Live resize as a first-class feature**: `VCK::HandleLiveResize(window, ...)` handles any resize including 720p → 4K. One call per frame. The scheduler-aware overload (v0.3) drains via `FrameScheduler::DrainInFlight()` instead of `vkDeviceWaitIdle`.
- **Frame retirement in one word** (v0.3): `FrameScheduler` owns one `TimelineSemaphore`; `EndFrame` signals a monotonic per-slot value and `BeginFrame` waits on it with a single `vkWaitSemaphores`. Fence-path fallback when `VK_KHR_timeline_semaphore` isn't available.
- **Dedicated compute / transfer queues** (v0.3): `VulkanDevice` picks compute-only + transfer-only families where the vendor exposes them; VMM staging submits to the dedicated transfer queue with release/acquire ownership barriers back to graphics.
- **Secondary command buffers** (v0.3): `VulkanCommand::AllocateSecondary / BeginSecondary / EndSecondary / ExecuteSecondaries` for multi-threaded record-then-execute patterns.
- **Ergonomic shader API** (v0.2.1): `VCKMath` (Vec2/3/4, Mat4, Perspective/LookAt), `VertexLayout`, `PushConstants`, `Primitives::Cube/Plane/Sphere/Quad/Line` — cube setup goes from ~40 lines of vertex tables to one call.
- **Anti-aliasing framework**: `cfg.aa.technique = AATechnique::Auto` runs a 5-step decision tree (VRAM tier → forward path → motion vectors → pick) once at `Swapchain::Initialize`. Sample-based (MSAA / A2C / SampleRate) is implemented; post-process names (FXAA / SMAA / TAA / TAAU) are returned to the renderer.
- **Structured logging**: `VCK::VCKLog` with `Info` / `Notice` / `Warn` / `Error` levels, console-spam dedup, `cfg.debug` opt-in. `VK_CHECK` routes failures to `Error` directly — fail loud by default.
- **22 design rules** enforced: explicit over magic, no hidden state, frame is the unit of truth, external synchronisation, zero cost for unused features, every public API has an example, `VCK.h` is the API surface. See [`docs/Design.md`](docs/Design.md).
- **[Cookbook](docs/Cookbook.md)** — copy-paste recipes for things the kit doesn't ship (image / OBJ loading, cube / line / SDF / text rendering, FXAA / SMAA / TAA, ImGui, offscreen PNG readback).

## Layers

```
 layers/core/            VCKCrossplatform + Vulkan*.{h,cpp}
    ↓                    window, instance, device, swapchain, pipeline, command, sync
 layers/expansion/       VCKExpansion.{h,cpp}
    ↓                    textures, meshes, samplers, descriptors, mipmaps
                         + HandleLiveResize (base + depth)
 layers/execution/       VCKExecution.{h,cpp}
    ↓                    FrameScheduler, JobGraph, TimelineSemaphore,
                         DebugTimeline, QueueSet, GpuSubmissionBatcher,
                         + timeline-aware HandleLiveResize
 layers/vmm/             VulkanMemoryManager.{h,cpp}  (optional)
    ↓                    staging ring, transient pool, persistent registry
 Your renderer
```

Everything lives in `namespace VCK`. `LogVk` and `VK_CHECK` are at global
scope so any TU can use them without a `using` declaration. The **single
source of truth** for the API surface is the doc block at the top of
[`VCK.h`](VCK.h) — layer files carry only a one-line "what am I" comment.

## One-hour tour

1. **Build one example.** `cd example && build.bat` → pick `[1]` (RGBTriangle). That's `main.cpp` + `App.h` + `App.cpp` + `assets/` — the whole onboarding surface. Full build steps: [`docs/Build.md`](docs/Build.md).
2. **Read `Hello VCK` below.** ~50 lines, one TU, no hidden state. Walks the entire init chain → frame loop → shutdown.
3. **Skim [`VCK.h`](VCK.h)** top-to-bottom. Big ASCII banners split it into CLASSES / IMPLS. Every class has a doc block explaining what it owns, who calls it, and the init/shutdown order.
4. **Decide what you want next**:
   - *Render something textured / indexed:* [`docs/Examples.md`](docs/Examples.md) — RGBTriangle, MipmapExample.
   - *Let VCK drive the frame loop:* [`docs/Execution-Layer.md`](docs/Execution-Layer.md) — FrameScheduler / JobGraph.
   - *Stop writing VMA boilerplate:* [`docs/VMM.md`](docs/VMM.md) — three-layer memory manager.
5. **Tune with `VCK::Config`** (below) when you need a different present mode, MSAA, or more frames in flight.

Total surface: **one header**, everything in `namespace VCK`, no opaque ownership. When in doubt, the raw-handle overload (`dev.Initialize(VkInstance, VkSurfaceKHR)`, etc.) is always still there for full manual control.

## Hello VCK

Minimal program that stands up a window, the core stack, and clears the
swapchain to a colour every frame through `FrameScheduler`. The full
three-file version lives in [`example/HelloExample/`](example/HelloExample/).

```cpp
#include "VCK.h"      // pulls in VCK::Window, the core, expansion, everything

int main()
{
    // window (cross-platform facade - no raw GLFW/HWND in user code)
    VCK::Window            win;
    VCK::WindowCreateInfo  wci;
    wci.width = 800; wci.height = 600; wci.title = "Hello VCK";
    wci.resizable = true;
    win.Create(wci);

    // core
    VCK::VulkanContext   ctx;
    VCK::VulkanDevice    dev;
    VCK::VulkanSwapchain sc;
    VCK::VulkanCommand   cmd;
    VCK::VulkanSync      sync;

    ctx.Initialize (win, "hello");
    dev.Initialize (ctx);
    sc .Initialize (dev, ctx, win.GetWidth(), win.GetHeight());
    cmd.Initialize (dev);
    sync.Initialize(dev);

    // execution layer (optional)
    VCK::FrameScheduler sched;
    VCK::FrameScheduler::Config cfg;
    cfg.policy = VCK::FramePolicy::Pipelined;
    sched.Initialize(dev, cmd, sync, cfg);

    while (!win.ShouldClose())
    {
        win.PollEvents();
        VCK::Frame& f = sched.BeginFrame();

        uint32_t imageIndex = 0;
        vkAcquireNextImageKHR(dev.GetDevice(), sc.GetSwapchain(),
                              UINT64_MAX, f.ImageAvailable(),
                              VK_NULL_HANDLE, &imageIndex);

        // ... begin render pass, clear, end render pass on f.PrimaryCmd() ...

        // One-liner — wires ImageAvailable / RenderFinished on the frame's
        // own semaphores and submits f.PrimaryCmd() on the graphics queue.
        f.QueueGraphics();

        sched.EndFrame();

        VkSemaphore     renderDone = f.RenderFinished();
        VkSwapchainKHR  sw         = sc.GetSwapchain();
        VkPresentInfoKHR p{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        p.waitSemaphoreCount = 1;
        p.pWaitSemaphores    = &renderDone;
        p.swapchainCount     = 1;
        p.pSwapchains        = &sw;
        p.pImageIndices      = &imageIndex;
        vkQueuePresentKHR(dev.GetGraphicsQueue(), &p);
    }

    sched.Shutdown();
    sync.Shutdown(); cmd.Shutdown(); sc.Shutdown(); dev.Shutdown(); ctx.Shutdown();
    win.Destroy();
}
```

## Optional `VCK::Config`

Every `Initialize(...)` in the core has a zero-arg form *and* an overload that
takes a `const VCK::Config&` — one struct, nested by class. Pass only the
knobs you care about; everything else uses sensible defaults matching the
zero-arg behaviour.

```cpp
VCK::Config cfg;
cfg.context.appName        = "Hello VCK";
cfg.context.enableValidation = true;                             // debug only
cfg.device.preferDiscreteGpu = true;
cfg.swapchain.presentMode  = VCK::PresentMode::Mailbox;          // Auto | Fifo | Mailbox | Immediate
cfg.swapchain.imageCount   = 3;                                  // 0 = minImageCount + 1
cfg.swapchain.msaaSamples  = VCK::MSAA_AUTO;                     // 0 = pick from device; or VK_SAMPLE_COUNT_{1,2,4,8}_BIT
cfg.aa.technique           = VCK::AATechnique::Auto;             // Auto | Off | MSAA | MSAA_A2C | SampleRate | FXAA | ...
cfg.aa.forwardRenderer     = true;                               // detector input
cfg.aa.supportsMotionVectors = false;                            // detector input
cfg.debug                  = false;                              // true = VCKLog Info lines visible
cfg.sync.framesInFlight    = 3;                                  // clamped to MAX_FRAMES_IN_FLIGHT

ctx.Initialize (window, cfg);
dev.Initialize (ctx,    cfg);
sc .Initialize (dev, ctx, window.GetWidth(), window.GetHeight(), cfg);
sync.Initialize(dev,  cfg);
cmd.Initialize (dev,  cfg);
pipe.Initialize(dev, sc, shaders, vi);    // pulls msaaSamples from the swapchain
```

Mailbox requests fall back to FIFO if the driver doesn't expose it, and
`framesInFlight` is clamped to the compile-time upper bound
`VCK::MAX_FRAMES_IN_FLIGHT` (= 8). v0.3 enables `VK_KHR_timeline_semaphore`
at device-create time when the adapter supports it (functionally every
modern GPU); `FrameScheduler` uses it by default.

## Examples

Thirteen runnable examples in `example/`. All follow a 3-file + `assets/`
layout (`main.cpp` + `App.h` + `App.cpp` + `assets/`), all use the
cross-platform `VCK::Window` facade and `VCK::HandleLiveResize` (so resizing
from 720p to 4K is handled in-library). Build with:

- Windows: `example/build.bat` (CMake + Ninja under the hood; cl from a Developer Cmd Prompt or MinGW g++ on `PATH`)
- Linux / macOS: `example/build.sh` (CMake + Ninja under the hood; g++ / clang++ from `PATH`)
- Anywhere: `cmake -S example -B build -G Ninja && cmake --build build -j` (the canonical command - `build.bat` / `build.sh` are thin wrappers around it)

| #  | Example                     | Demonstrates |
|----|-----------------------------|--------------|
|  1 | `RGBTriangle`               | coloured triangle, live resize — raw core |
|  2 | `MipmapExample`             | texture upload + mip generation + sampling — raw core |
|  3 | `VMMExample`                | VMM persistent / transient / staging — raw core + VMM |
|  4 | `SecondaryCmdExample`       | secondary command buffers + scheduler-aware resize (v0.3) |
|  5 | `DebugTimelineExample`      | span recorder + `Dump` every 120 frames — debug tooling |
|  6 | `DebugShowcaseExample`      | every `VCKLog` level, dedup, `VK_CHECK` path |
|  7 | `AAShowcaseExample`         | `DetectRecommendedAA` matrix + live auto-pick — expansion |
|  8 | `JobGraphExample`           | CPU task graph with dependencies — execution layer |
|  9 | `SubmissionBatchingExample` | 2 cmd buffers → 1 `vkQueueSubmit` |
| 10 | `TimelineExample`           | `TimelineSemaphore` + `DependencyToken` |
| 11 | `SchedulerPolicyExample`    | live-swap Lockstep / Pipelined / AsyncMax |
| 12 | `HelloExample`              | smallest `FrameScheduler` program — mostly VCK |
| 13 | `EasyCubeExample`           | `Primitives::Cube` + `VertexLayout` + `PushConstants` + `VCKMath` — peak ergonomic |

Full walkthroughs: [`docs/Examples.md`](docs/Examples.md).

## Build

Windows — CMake + Ninja picks whichever C++ compiler is on `PATH`.  Run
from a Developer Cmd Prompt and `cl` is used; otherwise MinGW-w64 `g++`
(MSYS2's `C:\msys64\mingw64\bin`) is picked up automatically:

```
cd example
build.bat                    :: interactive menu, picks compiler from PATH
build.bat A                  :: build all 13 examples
build.bat T                  :: build + run the R14 unit-test harness
:: or skip the wrapper entirely:
cmake -S . -B build -G Ninja
cmake --build build -j --target examples
```

Linux / macOS (`pkg-config vulkan glfw3` + `glslangValidator` + `g++` or `clang++`):

```
cd example
./build.sh
```

Both scripts share the same `[1]-[13] / [A] / [T] / [0]` menu and print a
diagnostic if tools or dependencies are missing. `[T]` builds and links
the R14 unit-test harness against the lib-once `vck.lib` / `libvck.a` and
runs it. Full step-by-step: [`docs/Build.md`](docs/Build.md).

Both scripts use a **lib-once compile model** (PR #7): the VCK static
library compiles once into `build/vck.lib` (cl) or `build/libvck.a`
(gcc/clang) and every example links against it (`main.cpp` + `App.cpp`
only). Build-all wall-clock on a modern 8-core machine: ~30-40 s on
Linux/macOS, ~2-3 min on Windows MSVC `/MP`, ~3-4 min on MinGW.

## Documentation

The README is deliberately short. Everything else lives under
[`docs/`](docs/) — these files mirror the GitHub Wiki layout, so the
maintainer can push them to the `*.wiki.git` repo at any time.

| Page | Covers |
|------|--------|
| [`docs/Home.md`](docs/Home.md)                     | index + layer map |
| [`docs/Overview.md`](docs/Overview.md)             | **one-page tour**: what VCK is / gives / never takes, modern v0.3 optimisations, why it's fresh |
| [`docs/Core-API.md`](docs/Core-API.md)             | `VCK.h` — Context / Device / Swapchain / Pipeline / Command / Sync / Buffer / Image |
| [`docs/Expansion-API.md`](docs/Expansion-API.md)   | `VCKExpansion.h` — framebuffers, depth, samplers, textures, meshes, descriptors, mipmaps |
| [`docs/Execution-Layer.md`](docs/Execution-Layer.md) | `FrameScheduler`, `JobGraph`, `GpuSubmissionBatcher`, `BackpressureGovernor`, `TimelineSemaphore`, `QueueSet`, `DebugTimeline` |
| [`docs/VMM.md`](docs/VMM.md)                       | `VulkanMemoryManager` — lifetimes, strategies, staging |
| [`docs/Examples.md`](docs/Examples.md)             | per-example walkthrough |
| [`docs/Build.md`](docs/Build.md)                   | Windows / Linux / macOS: Vulkan SDK + GLFW + `build.bat` / `build.sh` |
| [`docs/Design.md`](docs/Design.md)                 | design rules, status, caveats, roadmap |

## Contributing

PRs welcome. Read [`CONTRIBUTING.md`](CONTRIBUTING.md) for the 22-rule
architectural contract, commit style, and branching workflow. Small,
focused diffs preferred; new public API goes in [`VCK.h`](VCK.h) (the
single source of truth).

## Changelog

See [`CHANGELOG.md`](CHANGELOG.md) for the per-version list of added,
changed, and removed items. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## License

[MIT](LICENSE) — vendored third-party sources in `vendor/` keep their own
licenses (Apache 2.0 for Vulkan-Headers, MIT for VMA, zlib/libpng for
GLFW). See the [`LICENSE`](LICENSE) file for the full list.
