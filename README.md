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
    dev.Initialize (ctx.GetInstance(), ctx.GetSurface());
    sc .Initialize (dev, ctx.GetSurface(), 800, 600);
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

        VCK::GpuSubmissionBatcher::SubmitInfo si;
        si.waitSem   = f.ImageAvailable();
        si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(f.PrimaryCmd(), si);

        sched.EndFrame();

        VkPresentInfoKHR p{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        p.waitSemaphoreCount = 1;
        p.pWaitSemaphores    = &f.RenderFinished();
        VkSwapchainKHR sw = sc.GetSwapchain();
        p.swapchainCount = 1;
        p.pSwapchains    = &sw;
        p.pImageIndices  = &imageIndex;
        vkQueuePresentKHR(dev.GetGraphicsQueue(), &p);
    }

    sched.Shutdown();
    sync.Shutdown(); cmd.Shutdown(); sc.Shutdown(); dev.Shutdown(); ctx.Shutdown();
}
```

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
