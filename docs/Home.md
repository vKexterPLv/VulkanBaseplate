# VCK Wiki

Welcome to the **Vulkan Core Kit** wiki. The README in the repo is intentionally
short — everything below goes deep.

The files in this `docs/` folder mirror the GitHub Wiki page layout. Each page
name matches what it becomes on the Wiki once the maintainer pushes the pages
up (see [[Publishing this wiki]](#publishing-this-wiki)).

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

## Publishing this wiki

GitHub Wikis are separate git repos at `<repo>.wiki.git`. To populate the Wiki
from these files:

1. Visit the **Wiki** tab of the repo on GitHub once and click _"Create the
   first page"_ to initialise the wiki repo.
2. Clone and push:
   ```
   git clone https://github.com/vKexterPLv/VCK.wiki.git
   cp docs/*.md VCK.wiki/
   cd VCK.wiki
   git add . && git commit -m "Seed wiki from docs/"
   git push
   ```
3. On the Wiki, rename `Home.md` to the `Home` page (GitHub strips the `.md`
   extension automatically). Page names with hyphens, like `Core-API.md`,
   show up as `Core API` in the Wiki sidebar.
