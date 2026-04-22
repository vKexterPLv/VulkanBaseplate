# VCK Wiki

Welcome to the **Vulkan Core Kit** wiki. The README in the repo is intentionally
short — everything below goes deep.

The files in this `docs/` folder mirror the GitHub Wiki page layout — every
page name on the Wiki matches the filename here.

## One-hour onboarding path

If you want to be productive in ~60 minutes, follow this order:

1. **[[Build]](Build.md)** — 5 min to install SDK / MinGW / GLFW drop and run `example/build.bat`.
2. **[[Examples]](Examples.md) → RGBTriangle** — skim `main.cpp` + `App.h` + `App.cpp`. That's the whole onboarding surface for a triangle with live resize.
3. **[[Core API]](Core-API.md)** — 15 min. Read the `VCK::Config` section; everything else is short per-class reference you can scan.
4. **[[Execution Layer]](Execution-Layer.md) → HelloExample** — 15 min. This is where `FrameScheduler` takes over the per-frame dance.
5. **[[VMM]](VMM.md)** — 10 min only when you need VMM (textures/meshes/GPU-only buffers) instead of raw `VulkanBuffer`/`VulkanImage`.

That's the whole library. The rest of this wiki is deep-dive reference.

## Pages

| Page | Covers |
|------|--------|
| [[Core API]](Core-API.md)             | `VCK.h` — Context, Device, Swapchain, Pipeline, Command, Sync, Buffer, Image |
| [[Expansion API]](Expansion-API.md)   | `VCKExpansion.h` — framebuffers, depth, samplers, textures, meshes, descriptors, mipmaps |
| [[Execution Layer]](Execution-Layer.md) | `FrameScheduler`, `JobGraph`, `GpuSubmissionBatcher`, `BackpressureGovernor`, `TimelineSemaphore`, `DependencyToken`, `QueueSet`, `DebugTimeline` |
| [[VMM]](VMM.md)                       | `VulkanMemoryManager` — persistent / transient / frame-buffered lifetimes, staging ring |
| [[Examples]](Examples.md)             | Walk-through of the 9 runnable examples in `example/` |
| [[Build]](Build.md)                   | Windows + MinGW + Vulkan SDK + GLFW setup, `build.bat` menu |
| [[Design]](Design.md)                 | Design rules, status, caveats, roadmap |

## Layer map

```
 VCK core                 VCK.h + Vulkan*.cpp
    ↓                     instance, device, swapchain, pipeline, command, sync
 VCK expansion            VCKExpansion.h / .cpp
    ↓                     textures, meshes, samplers, descriptors, mipmaps
 VCK execution            FrameScheduler, JobGraph, TimelineSemaphore, ...
    ↓                     (same VCKExpansion TU — opt in)
 VCK memory (VMM)         VMM/VulkanMemoryManager.{h,cpp}  (optional)
    ↓                     staging ring, transient pool, persistent registry
 Your renderer
```

Everything lives in `namespace VCK`. `LogVk` and `VK_CHECK` are global so any
TU can use them without a `using` declaration.
