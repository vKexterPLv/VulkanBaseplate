<div align="center">

# VCK &nbsp;·&nbsp; Vulkan Core Kit

**Tiny, honest, zero-magic Vulkan layer for Windows / MinGW.**
A layer you *opt into*, never one that takes over.

[![Windows build](https://github.com/vKexterPLv/VCK/actions/workflows/build.yml/badge.svg?branch=VCK)](https://github.com/vKexterPLv/VCK/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/vKexterPLv/VCK/blob/VCK/LICENSE)
[![Docs](https://img.shields.io/badge/docs-wiki-informational)](https://github.com/vKexterPLv/VCK/wiki)
[![One include](https://img.shields.io/badge/%23include-%22VCK.h%22-success)](Core-API)
[![Examples](https://img.shields.io/badge/examples-9-orange)](Examples)

</div>

---

## At a glance

```cpp
#include "VCK.h"                                    // the whole core API in one line

VCK::Window   win;
VCK::Config   cfg;                                  // master config - zero-init = sensible defaults
VCK::VulkanContext   ctx;
VCK::VulkanDevice    dev;
VCK::VulkanSwapchain sc;

win.Create({ 1280, 720, "Hello, VCK" });
ctx.Initialize(win.NativeHandle(),  GetModuleHandle(nullptr), cfg);
dev.Initialize(ctx,                                          cfg);
sc .Initialize(dev, ctx, 1280, 720,                          cfg);
```

Three lines of setup, one include, every raw-handle form still available for when you want full control.

---

## Contents

- [One-hour onboarding path](#one-hour-onboarding-path)
- [Pages](#pages)
- [Layer map](#layer-map)
- [Design rules](#design-rules)

---

## One-hour onboarding path

If you want to be productive in ~60 minutes, follow this order:

| # | Step | Time | Link |
|---|------|------|------|
| 1 | Install SDK / MinGW / GLFW drop, run `example/build.bat` | 5 min | [Build](Build) |
| 2 | Skim `RGBTriangle/main.cpp` + `App.{h,cpp}` | 10 min | [Examples](Examples) |
| 3 | Read the `VCK::Config` section; the rest is per-class reference | 15 min | [Core API](Core-API) |
| 4 | Read `HelloExample` — `FrameScheduler` takes over the per-frame dance | 15 min | [Execution Layer](Execution-Layer) |
| 5 | Only when you need persistent / transient / frame-buffered GPU memory | 10 min | [VMM](VMM) |

That's the whole library. The rest of this wiki is deep-dive reference.

---

## Pages

| Page | Covers |
|------|--------|
| [Core API](Core-API)                 | `VCK.h` — Context, Device, Swapchain, Pipeline, Command, Sync, Buffer, Image, Window |
| [Expansion API](Expansion-API)       | `VCKExpansion.h` — framebuffers, depth, samplers, textures, meshes, descriptors, mipmaps |
| [Execution Layer](Execution-Layer)   | `FrameScheduler`, `JobGraph`, `GpuSubmissionBatcher`, `BackpressureGovernor`, `TimelineSemaphore`, `DependencyToken`, `QueueSet`, `DebugTimeline` |
| [VMM](VMM)                           | `VulkanMemoryManager` — persistent / transient / frame-buffered lifetimes, staging ring |
| [Examples](Examples)                 | Walk-through of the 9 runnable examples in `example/` |
| [Build](Build)                       | Windows + MinGW + Vulkan SDK + GLFW setup, `build.bat` menu |
| [Design](Design)                     | Design rules, status, caveats, roadmap |

---

## Layer map

```
 ┌─────────────────────────────────────────────────────────────┐
 │  Your renderer                                              │
 └─────────────────────────────────────────────────────────────┘
                           ▲  opts in, one layer at a time
 ┌─────────────────────────────────────────────────────────────┐
 │  VMM            VulkanMemoryManager  (persistent / transient│
 │  (optional)                            / frame-buffered)    │
 ├─────────────────────────────────────────────────────────────┤
 │  Execution      FrameScheduler · JobGraph · TimelineSemaphore│
 │                 GpuSubmissionBatcher · BackpressureGovernor │
 ├─────────────────────────────────────────────────────────────┤
 │  Expansion      framebuffers · depth · samplers · textures  │
 │                 meshes · descriptors · mipmaps              │
 ├─────────────────────────────────────────────────────────────┤
 │  Core           Context · Device · Swapchain · Pipeline     │
 │                 Command · Sync · Buffer · Image · Window    │
 ├─────────────────────────────────────────────────────────────┤
 │  Vulkan 1.3                                                 │
 └─────────────────────────────────────────────────────────────┘
```

Everything lives in `namespace VCK`. `VCKLog::Info("Tag", "msg")` (and legacy `LogVk("msg")`) + `VK_CHECK` are global so any TU can use them without a `using` declaration.

---

## Design rules

- **One include** &mdash; `#include "VCK.h"` pulls the entire core API.
- **No hidden ownership** &mdash; every class is a handle you construct and `Destroy()` yourself.
- **No imposed allocations** &mdash; VMM is optional; `VulkanBuffer`/`VulkanImage` stay raw.
- **Additive overloads** &mdash; every `Initialize(cfg)` is one line forwarding to the raw-handle form.
- **Full escape hatches** &mdash; `NativeHandle()`, `Get*()`, raw `Initialize(VkXxx)` always available.
- **Colour-coded log** &mdash; `VCKLog::Info/Warn/Error(tag, msg)` produces `[VCK] [Tag] msg` so the console is scannable at a glance.

See [Design](Design) for the full list and the honest "what isn't wired yet" roadmap.
