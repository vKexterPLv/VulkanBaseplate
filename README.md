<div align="center">

# V C K

**Vulkan Core Kit** — a small, no-magic Vulkan kit for Windows, plus a
frame-level execution orchestration layer on top.

<sub>**not** an engine · no scene graph · no material system · you own the frame</sub>

</div>

---

Drop one header:

```cpp
#include "VCK.h"   // auto-includes VCKExpansion.h
```

…and you get the full kit: core Vulkan objects, higher-level building blocks
(textures, meshes, descriptors, mipmaps), an optional memory manager (VMM),
and an optional frame scheduler with a CPU job graph + GPU submission
batcher.

## Layers

```
 VCK core                VCK.h + Vulkan*.cpp
    ↓                    instance, device, swapchain, pipeline, command, sync
 VCK expansion           VCKExpansion.h / .cpp
    ↓                    textures, meshes, samplers, descriptors, mipmaps
 VCK execution           FrameScheduler, JobGraph, TimelineSemaphore, ...
    ↓                    (same VCKExpansion TU — opt in)
 VCK memory (VMM)        VMM/VulkanMemoryManager.{h,cpp}  (optional)
    ↓                    staging ring, transient pool, persistent registry
 Your renderer
```

Everything lives in `namespace VCK`. `LogVk` and `VK_CHECK` are at global
scope so any TU can use them without a `using` declaration.

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
#include "VCK.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

int main()
{
    // window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow(800, 600, "Hello VCK", nullptr, nullptr);

    // core
    VCK::VulkanContext   ctx;
    VCK::VulkanDevice    dev;
    VCK::VulkanSwapchain sc;
    VCK::VulkanCommand   cmd;
    VCK::VulkanSync      sync;

    ctx.Initialize (glfwGetWin32Window(win), "hello");
    dev.Initialize (ctx);
    sc .Initialize (dev, ctx, 800, 600);
    cmd.Initialize (dev);
    sync.Initialize(dev);

    // execution layer (optional)
    VCK::FrameScheduler sched;
    VCK::FrameScheduler::Config cfg;
    cfg.policy = VCK::FramePolicy::Pipelined;
    sched.Initialize(dev, cmd, sync, cfg);

    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
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
// cfg.swapchain.msaaSamples = VK_SAMPLE_COUNT_4_BIT;            // reserved — see Roadmap, clamps to 1x today
cfg.sync.framesInFlight    = 3;                                  // clamped to MAX_FRAMES_IN_FLIGHT

ctx.Initialize (hwnd, cfg);
dev.Initialize (ctx,  cfg);
sc .Initialize (dev, ctx, w, h, cfg);
sync.Initialize(dev,  cfg);
cmd.Initialize (dev,  cfg);
pipe.Initialize(dev, sc, shaders, vi);    // pulls msaaSamples from the swapchain
```

Mailbox requests fall back to FIFO if the driver doesn't expose it, and
`framesInFlight` is clamped to the compile-time upper bound
`VCK::MAX_FRAMES_IN_FLIGHT` (= 3). Deeper pipelining needs
`VK_KHR_timeline_semaphore` — a separate track.

## Examples

Nine runnable examples in `example/`. All follow a 3-file + `assets/` layout
(`main.cpp` + `App.h` + `App.cpp` + `assets/`). Build with the menu in
`example/build.bat`.

| # | Example                     | Demonstrates |
|---|-----------------------------|--------------|
| 1 | `RGBTriangle`               | coloured triangle, live resize |
| 2 | `MipmapExample`             | texture upload + mip generation + sampling |
| 3 | `VMMExample`                | VMM persistent / transient / staging |
| 4 | `HelloExample`              | smallest `FrameScheduler` program |
| 5 | `JobGraphExample`           | CPU task graph with dependencies |
| 6 | `SchedulerPolicyExample`    | live-swap Lockstep / Pipelined / AsyncMax |
| 7 | `SubmissionBatchingExample` | 2 cmd buffers → 1 `vkQueueSubmit` |
| 8 | `TimelineExample`           | `TimelineSemaphore` + `DependencyToken` |
| 9 | `DebugTimelineExample`      | span recorder + `Dump` every 120 frames |

Full walkthroughs: [`docs/Examples.md`](docs/Examples.md).

## Build

```
cd example
build.bat
```

Requires the Vulkan SDK, `glslangValidator`, `g++` (MinGW-w64), and a
GLFW Windows pre-compiled drop in `example/deps/`. Full step-by-step:
[`docs/Build.md`](docs/Build.md). `build.bat` prints a diagnostic if
anything is missing.

## Documentation

The README is deliberately short. Everything else lives under
[`docs/`](docs/) — these files mirror the GitHub Wiki layout, so the
maintainer can push them to the `*.wiki.git` repo at any time.

| Page | Covers |
|------|--------|
| [`docs/Home.md`](docs/Home.md)                     | index + layer map |
| [`docs/Core-API.md`](docs/Core-API.md)             | `VCK.h` — Context / Device / Swapchain / Pipeline / Command / Sync / Buffer / Image |
| [`docs/Expansion-API.md`](docs/Expansion-API.md)   | `VCKExpansion.h` — framebuffers, depth, samplers, textures, meshes, descriptors, mipmaps |
| [`docs/Execution-Layer.md`](docs/Execution-Layer.md) | `FrameScheduler`, `JobGraph`, `GpuSubmissionBatcher`, `BackpressureGovernor`, `TimelineSemaphore`, `QueueSet`, `DebugTimeline` |
| [`docs/VMM.md`](docs/VMM.md)                       | `VulkanMemoryManager` — lifetimes, strategies, staging |
| [`docs/Examples.md`](docs/Examples.md)             | per-example walkthrough |
| [`docs/Build.md`](docs/Build.md)                   | Windows + MinGW + Vulkan SDK + GLFW setup |
| [`docs/Design.md`](docs/Design.md)                 | design rules, status, caveats, roadmap |

## License

See the repository for license information.
