<div align="center">

# VMM &nbsp;·&nbsp; Vulkan Memory Manager

Persistent · transient · frame-buffered — three lifetimes, one staging ring

</div>

---

Three-layer GPU resource lifecycle. Optional — the core works fine with raw
`VulkanBuffer` / `VulkanImage`. Use VMM when you want batched staging
uploads, a zero-cost per-frame transient pool, or lifetime-tagged
persistent allocations.

---

## Layers

```
Layer 1  VmmRawAlloc          thin typed VMA wrappers
Layer 2  VmmRegistry          every allocation tagged with a Lifetime
Layer 3  VulkanMemoryManager  pools and strategies
```

## Lifetimes

| Lifetime         | Behaviour |
|------------------|-----------|
| `Persistent`     | Freed at `Shutdown` or explicit `FreeBuffer` / `FreeImage`. |
| `TransientFrame` | Auto-freed when the frame slot it was allocated in comes around again. |
| `FrameBuffered`  | One instance per frame slot. |
| `Manual`         | Registry tracks it for stats only; caller frees it. |

## Strategies

- **StagingRing** — single CPU-visible ring for batched uploads.
  `StageToBuffer` / `StageToImage` queue copies; `EndFrame` /
  `FlushStaging` submit the batch.
- **TransientPool** — one CPU→GPU block per frame slot, persistently mapped.
  Zero-cost pointer bumps; reset on `BeginFrame`.
- **Persistent allocations** — textures, static meshes, anything that
  outlives a frame.

## Typical frame (outside FrameScheduler)

```cpp
vkWaitForFences(..., sync.GetInFlightFence(slot), ...);   // MUST come first
vmm.BeginFrame(slot, absoluteFrame);

VCK::VmmBuffer dyn = vmm.AllocTransient(slot, "ui_verts",
                                        size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
dyn.Upload(data, size);

// ... record draws ...

vmm.EndFrame(slot);   // submits staging batch, resets ring
```

## Persistent + staged upload

```cpp
VCK::VmmBuffer vb = vmm.AllocPersistent("static-verts", size,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT);
vmm.StageToBuffer(vb, src, size);
vmm.FlushStaging();     // one-shot submit + wait + ring reset
```

## Persistent image + staged upload

```cpp
VCK::VmmImage img = vmm.AllocPersistentImage(
    "texture", width, height, VK_FORMAT_R8G8B8A8_SRGB,
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
vmm.StageToImage(img, pixels, pixelsBytes);
vmm.FlushStaging();
```

## Stats

```cpp
vmm.LogStats();    // registry + staging ring + transient pool snapshot
```

Use it as a sanity check: it prints per-lifetime counts, total bytes, and
the staging ring's current high-water mark.

## Current caveats

- **v0.3**: `VulkanMemoryManager::SubmitStagingCmd` now uses a per-submit
  `VkFence` (no `vkQueueWaitIdle`). When the transfer queue is dedicated,
  staging submits to the transfer family and records a release/acquire
  ownership-barrier pair so the graphics queue sees the expected image
  layout (Vulkan §7.7.4). The acquire is CPU-serialised today — a
  semaphore-driven async acquire is on the v0.4 roadmap.
- **v0.3**: The staging ring still resets at the end of each `EndFrame` /
  `FlushStaging`; per-frame staging is still the recommended pattern for
  pipelined uploads.
- `VulkanOneTimeCommand::End` uses a short-circuiting `vkWaitForFences` so a
  failed submit no longer hangs the caller (v0.3 fix).
