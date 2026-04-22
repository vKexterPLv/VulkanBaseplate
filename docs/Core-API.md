# Core API — `VCK.h`

One header (`VCK.h`) + a matching set of `Vulkan*.cpp` translation units. Owns
the raw Vulkan handles. Nothing above this layer creates or destroys these
objects.

## Classes

| Class              | Owns |
|--------------------|------|
| `VulkanContext`    | `VkInstance`, debug messenger, Win32 `VkSurfaceKHR` |
| `VulkanDevice`     | physical + logical device, VMA `VmaAllocator`, graphics + present queues |
| `VulkanSwapchain`  | `VkSwapchainKHR` + image views, default render pass, recreate path |
| `VulkanPipeline`   | graphics pipeline + pipeline layout builder |
| `VulkanCommand`    | command pool + one primary command buffer per frame slot |
| `VulkanSync`       | image-available / render-finished semaphores + in-flight fences |
| `VulkanBuffer`     | `VkBuffer` + VMA allocation, optional persistent map, `Upload()` |
| `VulkanImage`      | `VkImage` + view + VMA allocation, layout transition helper |

## Globals

- `LogVk(std::string)` — writes to **both** `OutputDebugStringA` (VS Output)
  and `stdout` + `fflush(stdout)`. Safe to call from anywhere; no
  initialization required.
- `VK_CHECK(expr)` — wraps any `VkResult`-returning call. On non-success
  prints the expression and the `VkResult` via `LogVk`. Use it everywhere:
  ```cpp
  VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
  ```
- `MAX_FRAMES_IN_FLIGHT` = `2`.

## Lifecycle

```
Init:     Context → Device → Swapchain → Pipeline → Command → Sync
Shutdown: Sync → Command → Pipeline → Swapchain → Device → Context
```

Expansion objects, VMM resources, and any `FrameScheduler` must be shut down
**before** the core objects they reference.

## Minimal use

```cpp
VCK::VulkanContext   ctx;
VCK::VulkanDevice    dev;
VCK::VulkanSwapchain sc;
VCK::VulkanPipeline  pipe;
VCK::VulkanCommand   cmd;
VCK::VulkanSync      sync;

ctx.Initialize (glfwGetWin32Window(win), "myapp");
dev.Initialize (ctx);
sc .Initialize (dev, ctx, 800, 600);
// ... pipeline setup ...
cmd.Initialize(dev);
sync.Initialize(dev);

// per-frame:
//   vkWaitForFences(sync.GetInFlightFence(slot) ...)
//   vkAcquireNextImageKHR(..., sync.GetImageAvailable(slot), ...)
//   record into cmd.Get(slot)
//   vkQueueSubmit with sync.GetImageAvailable(slot) → sync.GetRenderFinished(slot)
//   vkQueuePresentKHR waiting on sync.GetRenderFinished(slot)

sync.Shutdown(); cmd.Shutdown(); pipe.Shutdown();
sc.Shutdown();   dev.Shutdown();  ctx.Shutdown();
```

If you want the scheduler to drive the per-frame steps for you, see
[[Execution Layer]](Execution-Layer.md) — the core API never changes.
