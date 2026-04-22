# VCK — Vulkan Core Kit

A small, opinionated, **no-magic** Vulkan kit for Windows.
Drop it into a project and start writing a renderer instead of writing 1500 lines of `vkCreate*` boilerplate.

VCK is **not an engine.** There is no scene graph, no material system, no update loop. You own the frame — VCK just gives you the pieces and stays out of the way.

---

## What's in the box

```
 VCK core           VCK.h + Vulkan*.cpp
    ↓               instance, device, swapchain, pipeline, command, sync
 VCK expansion      VCKExpansion.h / .cpp
    ↓               textures, meshes, samplers, descriptors, mipmaps
 VCK memory (VMM)   VMM/VulkanMemoryManager.{h,cpp}     (optional)
    ↓               staging ring, transient per-frame pool, persistent registry
 Your renderer
```

All three layers share the `namespace VCK { ... }`, and `LogVk` / `VK_CHECK` are at global scope.
You only ever include **one header**:

```cpp
#include "VCK.h"        // pulls in VCKExpansion.h automatically
// #include "VMM/VulkanMemoryManager.h"  // optional, if you want the memory manager
```

---

## Design rules

### 1. Explicit over magic
No hidden allocations, no singletons, no implicit lifetimes, no "smart" destructors trying to guess what you meant. Every `Initialize` has a matching `Shutdown`, in a documented order.

### 2. The expansion layer does not own core objects
Every class in `VCKExpansion.h` receives `VulkanDevice&`, `VulkanCommand&`, etc. by reference. It never creates or destroys them. This keeps lifetimes linear and circular dependencies impossible.

### 3. Strict lifecycle order

**Init**
```
Context → Device → Swapchain → Pipeline → Command → Sync
```

**Shutdown**
```
Sync → Command → Pipeline → Swapchain → Device → Context
```

Expansion objects and VMM resources **must be shut down before** the core objects they reference.

---

## Core (`VCK.h`)

| Class              | Owns |
|--------------------|------|
| `VulkanContext`    | `VkInstance`, debug messenger, Win32 `VkSurfaceKHR` |
| `VulkanDevice`     | physical + logical device, VMA `VmaAllocator`, queues |
| `VulkanSwapchain`  | `VkSwapchainKHR` + image views, default render pass, recreate path |
| `VulkanPipeline`   | graphics pipeline + pipeline layout builder |
| `VulkanCommand`    | command pool + one command buffer per frame slot |
| `VulkanSync`       | image-available / render-finished semaphores + in-flight fences, frame index |
| `VulkanBuffer`     | `VkBuffer` + VMA allocation, optional persistent map, `Upload()` |
| `VulkanImage`      | `VkImage` + view + VMA allocation, layout transition helper |

Plus the global utilities:
- `LogVk(std::string)` — writes to **both** `OutputDebugStringA` (VS Output) and `stdout` (console).
- `VK_CHECK(expr)` — lambda-wrapped `VkResult` checker that logs the failing call.
- `MAX_FRAMES_IN_FLIGHT` = `2`.

---

## Expansion (`VCKExpansion.h`)

Higher-level building blocks. All borrow core objects; none own them.

| Class                            | What it does |
|----------------------------------|--------------|
| `VulkanOneTimeCommand`           | one-shot GPU command (staging, layout transitions) — uses `vkQueueWaitIdle`, not per-frame |
| `VulkanFramebufferSet`           | one `VkFramebuffer` per swapchain image, optional depth attachment, recreate on resize |
| `VulkanDepthBuffer`              | depth/stencil image with automatic format selection |
| `VulkanSampler`                  | `VkSampler` factory — `CreateNearest` / `CreateLinear` |
| `VulkanTexture`                  | CPU pixels → GPU image, staging + transitions hidden |
| `VulkanMesh`                     | vertex + index buffers, `RecordDraw(cmd)` |
| `VulkanDescriptorLayoutBuilder`  | fluent `VkDescriptorSetLayout` builder |
| `VulkanDescriptorPool`           | pool + per-frame set allocation (one type) |
| `VulkanUniformSet<T>`            | typed per-frame UBO with `Write(frame, data)` + `GetSet(frame)` |
| `VulkanDescriptorAllocator`      | mixed-type pool, `Allocate(layout)` per set |
| `VulkanModelPipeline`            | full model pipeline: descriptor layouts + push constants + pipeline |
| `VulkanMipmapGenerator`          | blit-based mip chain generation for any `VkImage` |

---

## VMM — `VMM/VulkanMemoryManager.h` (optional)

A three-layer GPU memory manager that sits next to the core. You do **not** need it to use VCK — the core works fine with raw `VulkanBuffer` / `VulkanImage`.

```
Layer 1  VmmRawAlloc            thin typed VMA wrappers
Layer 2  VmmRegistry            every allocation tagged with a Lifetime
Layer 3  VulkanMemoryManager    pools and strategies on top
```

Lifetimes:
- `Persistent` — freed at `Shutdown()` or explicit `FreeBuffer` / `FreeImage`.
- `TransientFrame` — auto-freed when the frame slot it was allocated in comes around again.
- `FrameBuffered` — one instance per frame slot.
- `Manual` — registry tracks it for stats only; caller frees it.

Strategies:
- **`StagingRing`** — a single CPU-visible ring buffer for batched uploads. `StageToBuffer` / `StageToImage` queue a copy; `EndFrame` / `FlushStaging` submit the batch.
- **`TransientPool`** — one CPU→GPU block per frame slot, persistently mapped. Sub-allocations are zero-cost pointer bumps, reset every `BeginFrame`.
- **Persistent allocations** — textures, static meshes, anything that outlives a frame.

Typical frame:
```cpp
vkWaitForFences(..., sync.GetInFlightFence(frame), ...);  // MUST come first
vmm.BeginFrame(frame, absoluteFrame);

VmmBuffer dyn = vmm.AllocTransient(frame, "ui_verts",
                                   size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
dyn.Upload(data, size);

// ... record draws ...

vmm.EndFrame(frame);   // submits staging batch, resets ring
```

`vmm.LogStats()` prints a registry + ring snapshot to the console any time you want a sanity check.

---

## Examples

Three runnable examples live under `example/`, each built with the same `build.bat`:

| # | Example          | Demonstrates |
|---|------------------|--------------|
| 1 | `RGBTriangle`    | core-only smoke test — coloured triangle, live window resize |
| 2 | `MipmapExample`  | expansion layer — texture upload + mipmap generation + sampling |
| 3 | `VMMExample`     | VMM — static triangle (persistent + staging), animated quad (transient pool), checkerboard texture (persistent image + staging), `LogStats()` every 120 frames |

---

## Build (Windows)

### Requirements

- **Vulkan SDK** — set `VULKAN_SDK` to the install root (e.g. `C:\VulkanSDK\1.3.x.y`).
- **glslangValidator** on `PATH` (ships with the SDK).
- **g++ (MinGW-w64)** on `PATH`.
- **GLFW Windows pre-compiled** from <https://www.glfw.org>.

### Dependency layout (first time)

Lay the files out under `example/deps/` like this:

```
example/
  build.bat
  deps/
    vk_mem_alloc.h                 (from VMA repo)
    libglfw3.a                     (from GLFW lib-mingw-w64/)
    glfw/
      include/
        GLFW/
          glfw3.h
          glfw3native.h
```

### Build + run

```
cd example
build.bat
```

Pick an example from the menu. On success:

```
example\VMMExample\VMMExample.exe
```

Run it from `cmd.exe` — the exe is console-subsystem, so `LogVk` output shows up in the terminal you launched it from (and simultaneously in the Visual Studio Output window when attached).

---

## Logging

`LogVk(msg)` writes every line to **two** sinks:

1. `OutputDebugStringA` — Visual Studio / WinDbg Output window.
2. `stdout` — the console that launched the exe.

`VK_CHECK(expr)` wraps a `VkResult`-returning call and logs the failing call if it returns anything other than `VK_SUCCESS`.

No `AllocConsole` dance: `build.bat` produces a console-subsystem exe, so `stdout` is already wired to the launching terminal.

---

## Status / notes

- Per-frame resources must respect the fence. Always wait on `sync.GetInFlightFence(frame)` **before** `vmm.BeginFrame(frame, ...)` and before writing any per-slot memory (transient scratch, UBO, etc.) — anything that writes into memory the GPU may still be reading from the previous cycle of the same slot is a data race.
- `VulkanOneTimeCommand` and `VulkanMemoryManager::SubmitStagingCmd` both use `vkQueueWaitIdle`. Fine for setup and the current single-queue staging model; a candidate for replacement with a fence-per-submit path when you want truly non-blocking uploads.
- `VulkanMipmapGenerator` needs the image created with `VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`. Call `IsFormatSupported()` first and fall back to a single mip level if the device can't linearly blit that format.
- Everything targets **Windows + GLFW + MinGW-w64 g++**. Porting to Linux / MSVC is a mechanical exercise (swap `windows.h` / `OutputDebugStringA`, pick a non-Win32 surface extension) but is not done here.

---

## Roadmap (not promises)

- Fence-per-submit staging so `vkQueueWaitIdle` goes away.
- Render-graph-ish frame composition on top of `VulkanFramebufferSet` / `VulkanModelPipeline`.
- Multi-threaded command recording once there's a real workload to justify it.
- Bindless descriptor arrays for textures.

---

## License

MIT.
