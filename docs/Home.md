<div align="center">

# VCK &nbsp;·&nbsp; Vulkan Core Kit

**Tiny, honest, zero-magic Vulkan layer for Windows / Linux / macOS.**
A layer you *opt into*, never one that takes over.

[![Windows build](https://github.com/vKexterPLv/VCK/actions/workflows/build.yml/badge.svg?branch=VCK)](https://github.com/vKexterPLv/VCK/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/vKexterPLv/VCK/blob/VCK/LICENSE)
[![Docs](https://img.shields.io/badge/docs-wiki-informational)](https://github.com/vKexterPLv/VCK/wiki)
[![One include](https://img.shields.io/badge/%23include-%22VCK.h%22-success)](Core-API)
[![Examples](https://img.shields.io/badge/examples-13-orange)](Examples)

</div>

---

## At a glance

```cpp
#include "VCK.h"                                    // the whole kit in one line

VCK::Window          win;
VCK::Config          cfg;                           // zero-init = sensible defaults
VCK::VulkanContext   ctx;
VCK::VulkanDevice    dev;
VCK::VulkanSwapchain sc;

VCK::WindowCreateInfo wci;
wci.width = 1280; wci.height = 720; wci.title = "Hello, VCK"; wci.resizable = true;
win.Create(wci);

ctx.Initialize(win,                                       cfg);
dev.Initialize(ctx,                                       cfg);
sc .Initialize(dev, ctx, win.GetWidth(), win.GetHeight(), cfg);
```

Three lines of setup, one include, every raw-handle form still available for when you want full control. The single source of truth for the API surface is the header doc block at the top of [`VCK.h`](https://github.com/vKexterPLv/VCK/blob/VCK/VCK.h).

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
| 1 | Install SDK / g++ / GLFW drop, run `example/build.bat` (Win) or `./build.sh` (Linux/macOS) | 5 min | [Build](Build) |
| 2 | Follow the per-line walkthrough of `HelloExample` | 15 min | [Hello VCK](Hello-VCK) |
| 3 | Skim `VCK.h` header block — single source of truth for the API surface | 10 min | [Core API](Core-API) |
| 4 | Read `FrameScheduler`: takes over the per-frame dance + handles live resize | 15 min | [Execution Layer](Execution-Layer) |
| 5 | Only when you need persistent / transient / frame-buffered GPU memory | 10 min | [VMM](VMM) |
| 6 | `VCK::Config` deep dive: AA framework, logging, present modes | 5 min | [Core API](Core-API) |

That's the whole library. The rest of this wiki is deep-dive reference.

---

## Pages

| Page | Covers |
|------|--------|
| [Overview](Overview)                 | One-page tour: what VCK is / gives / never takes, modern optimisations (v0.3), why it's fresh, honest summary |
| [Core API](Core-API)                 | `VCK.h` — Context, Device, Swapchain, Pipeline, Command, Sync, Buffer, Image, Window |
| [Expansion API](Expansion-API)       | `VCKExpansion.h` — framebuffers, depth, samplers, textures, meshes, descriptors, mipmaps |
| [Execution Layer](Execution-Layer)   | `FrameScheduler`, `JobGraph`, `GpuSubmissionBatcher`, `BackpressureGovernor`, `TimelineSemaphore`, `DependencyToken`, `QueueSet`, `DebugTimeline` |
| [VMM](VMM)                           | `VulkanMemoryManager` — persistent / transient / frame-buffered lifetimes, staging ring |
| [Examples](Examples)                 | Walk-through of the 13 runnable examples in `example/` |
| [Hello VCK](Hello-VCK)               | Build your first app: per-line walkthrough of `HelloExample` with a "why you might need this" note on every call |
| [Cookbook](Cookbook)                 | Copy-paste recipes for things VCK doesn't ship: image / OBJ loading, cube / line / SDF / text rendering, FXAA / SMAA / TAA, ImGui, offscreen PNG readback |
| [Build](Build)                       | Windows (`build.bat`) + Linux/macOS (`build.sh`): Vulkan SDK + GLFW setup |
| [Design](Design)                     | 22 design rules, status, caveats, architecture |

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
 │                 VCKLog · VK_CHECK · AATechnique auto-detect│
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
