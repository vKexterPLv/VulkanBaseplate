# VCK — Vulkan Core Kit

Minimal, modular Vulkan helper library designed as a clean foundation for building a custom renderer from scratch.

This project provides a controlled Vulkan environment with **explicit lifetimes**, **predictable initialization order**, and a **layered architecture** separating low-level setup from higher-level rendering systems.

---

## CORE IDEA

This is **not an engine**.

This is a **kit** you can drop into a project and immediately start building a renderer on top of — without fighting Vulkan boilerplate.

---

## ARCHITECTURE

```
Core (VulkanModule)
    ↓
Expansion Layer
    ↓
Your Renderer / Systems
```

* **Core**: low-level Vulkan setup and ownership
* **Expansion**: reusable GPU abstractions
* **Top**: your actual rendering logic

---

## DESIGN RULES

### 1. Explicit > Magic

* No hidden ownership
* No implicit lifetimes
* No side effects

Everything is controlled and predictable.

---

### 2. No Ownership in Expansion Layer

All expansion classes:

* receive references or raw pointers to core objects
* do NOT create them
* do NOT destroy them

This guarantees:

* stable lifetimes
* no circular dependencies
* no hidden behavior

---

### 3. Strict Lifecycle Order

**Initialization**

```
Context → Device → Swapchain → Pipeline → Command → Sync
```

**Shutdown**

```
Sync → Command → Pipeline → Swapchain → Device → Context
```

Expansion objects must be destroyed **BEFORE** base objects.

---

## WHAT YOU GET

### Core Vulkan layer

* Instance + surface
* Physical & logical device selection
* Swapchain creation & recreation
* Graphics pipeline setup
* Command buffers per frame
* Synchronization (double buffering)
* VMA allocator integration

---

### Expansion Layer

* **VulkanOneTimeCommand**
  One-shot GPU commands (staging, layout transitions)

* **VulkanFramebufferSet**
  Per-swapchain framebuffer management

* **VulkanDepthBuffer**
  Depth/stencil attachment with automatic format selection

* **VulkanSampler**
  Simple sampler abstraction (nearest / linear)

* **VulkanTexture**
  Full CPU → GPU upload (staging + transitions hidden)

* **VulkanMesh**
  Vertex/index buffers with built-in draw recording

* **VulkanDescriptorLayoutBuilder**
  Fluent descriptor set layout creation

* **VulkanDescriptorPool**
  Per-frame descriptor allocation (double buffered)

* **VulkanUniformSet<T>**
  Typed per-frame uniform buffers with direct `Write()`

* **VulkanDescriptorAllocator**
  Mixed-type descriptor pool with per-set `Allocate()`

* **VulkanModelPipeline**
  Full model pipeline (UBOs, push constants, set layouts)

* **VulkanMipmapGenerator**
  Blit-based mip chain generation for any `VkImage`

---

## EXAMPLE USAGE

```cpp
VulkanMesh mesh;
mesh.Upload(device, command,
            vertices.data(), sizeof(vertices),
            indices.data(), indices.size());

mesh.RecordDraw(cmd);
```

```cpp
VulkanTexture texture;
texture.CreateFromPixels(device, command, pixels, width, height);
```

```cpp
ubo.Write(frameIndex, data);
vkCmdBindDescriptorSets(cmd, ...);
```

```cpp
// Mip chain generation (mip-aware VkImage required — see VulkanMipmapGenerator docs)
uint32_t mips = VulkanMipmapGenerator::MipLevels(width, height);
if (VulkanMipmapGenerator::IsFormatSupported(device, VK_FORMAT_R8G8B8A8_SRGB))
{
    VulkanMipmapGenerator gen;
    gen.Generate(device, command, rawImage, width, height, mips);
}
```

---

## NOTES

* `VulkanOneTimeCommand` uses `vkQueueWaitIdle`
  → intended for setup / upload, not per-frame usage

* `VulkanMipmapGenerator` requires the image to be created with
  `VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`.
  Call `IsFormatSupported()` before `Generate()` — fall back to a single
  mip level if the device doesn't support linear blitting for the format.

* Descriptor system is designed for future extension

---

## FUTURE DIRECTION

* Staging allocator / upload batching
* Material system
* Render graph / frame graph
* Bindless resources
* Multi-threaded command recording

---

## WHY THIS EXISTS

Most Vulkan examples are either:

* too low-level (triangle demos)
* too abstract (engine-level frameworks)

This project sits in between:

**full control + usable structure**

---

## LICENSE

MIT

* **VulkanUniformSet<T>**
  Typed per-frame uniform buffers with direct `Write()`

* **VulkanDescriptorAllocator**
  Mixed-type descriptor pool with per-set `Allocate()`

* **VulkanModelPipeline**
  Full model pipeline (UBOs, push constants, set layouts)

* **VulkanMipmapGenerator**
  Blit-based mip chain generation for any `VkImage`

---

## EXAMPLE USAGE

```cpp
VulkanMesh mesh;
mesh.Upload(device, command,
            vertices.data(), sizeof(vertices),
            indices.data(), indices.size());

mesh.RecordDraw(cmd);
```

```cpp
VulkanTexture texture;
texture.CreateFromPixels(device, command, pixels, width, height);
```

```cpp
ubo.Write(frameIndex, data);
vkCmdBindDescriptorSets(cmd, ...);
```

```cpp
// Mip chain generation (mip-aware VkImage required — see VulkanMipmapGenerator docs)
uint32_t mips = VulkanMipmapGenerator::MipLevels(width, height);
if (VulkanMipmapGenerator::IsFormatSupported(device, VK_FORMAT_R8G8B8A8_SRGB))
{
    VulkanMipmapGenerator gen;
    gen.Generate(device, command, rawImage, width, height, mips);
}
```

---

## NOTES

* `VulkanOneTimeCommand` uses `vkQueueWaitIdle`
  → intended for setup / upload, not per-frame usage

* `VulkanMipmapGenerator` requires the image to be created with
  `VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`.
  Call `IsFormatSupported()` before `Generate()` — fall back to a single
  mip level if the device doesn't support linear blitting for the format.

* Descriptor system is designed for future extension

---

## FUTURE DIRECTION

* Staging allocator / upload batching
* Material system
* Render graph / frame graph
* Bindless resources
* Multi-threaded command recording

---

## WHY THIS EXISTS

Most Vulkan examples are either:

* too low-level (triangle demos)
* too abstract (engine-level frameworks)

This project sits in between:

**full control + usable structure**

---

## LICENSE

MIT
