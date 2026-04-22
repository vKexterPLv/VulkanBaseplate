<div align="center">

# Core API &nbsp;·&nbsp; `VCK.h`

Instance · Device · Swapchain · Pipeline · Command · Sync · Buffer · Image · Window

</div>

---

One header (`VCK.h`) + a matching set of `core/Vulkan*.cpp` translation units.
Owns the raw Vulkan handles. Nothing above this layer creates or destroys
these objects.

---

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
- `VCK::MAX_FRAMES_IN_FLIGHT` = `3` — compile-time upper bound on
  frames-in-flight. The runtime count is set via `Config::SyncCfg::framesInFlight`
  (default `2`) and clamped to this bound.

## Lifecycle

```
Init:     Context → Device → Swapchain → Pipeline → Command → Sync
Shutdown: Sync → Command → Pipeline → Swapchain → Device → Context
```

Expansion objects, VMM resources, and any `FrameScheduler` must be shut down
**before** the core objects they reference.

## `VCK::Config` — optional init knobs

Every core `Initialize(...)` has a zero-arg form *and* an overload that takes
a `const VCK::Config&`. One struct, nested by class. Pass only the fields
you care about; everything else defaults to the previous zero-arg behaviour.

```cpp
struct VCK::Config
{
    struct ContextCfg {
        std::string              appName                 = "VCK App";
        bool                     enableValidation        = true;
        std::vector<const char*> extraInstanceLayers;
        std::vector<const char*> extraInstanceExtensions;
    } context;

    struct DeviceCfg {
        bool                     preferDiscreteGpu       = true;
        std::vector<const char*> extraDeviceExtensions;
        QueuePreference          queuePreference         = QueuePreference::GraphicsOnly;
    } device;

    struct SwapchainCfg {
        PresentMode              presentMode             = PresentMode::Mailbox; // falls back to FIFO automatically
        uint32_t                 imageCount              = 0;                    // 0 = minImageCount + 1
        VkFormat                 surfaceFormat           = VK_FORMAT_UNDEFINED;  // UNDEFINED = auto-pick
        VkSampleCountFlagBits    msaaSamples             = VK_SAMPLE_COUNT_1_BIT;
        VkFormat                 depthFormat             = VK_FORMAT_UNDEFINED;  // UNDEFINED = auto
    } swapchain;

    struct SyncCfg {
        uint32_t                 framesInFlight          = 2; // clamped to MAX_FRAMES_IN_FLIGHT
    } sync;
};

enum class PresentMode      { Auto, Fifo, Mailbox, Immediate };
enum class QueuePreference  { GraphicsOnly, GraphicsCompute, GraphicsComputeTransfer };
```

### Using Config

```cpp
VCK::Config cfg;
cfg.context.appName        = "myapp";
cfg.swapchain.presentMode  = VCK::PresentMode::Mailbox;    // tears-nothing, low-latency
cfg.sync.framesInFlight    = 3;                            // deeper CPU/GPU pipeline

ctx .Initialize(hwnd, cfg);
dev .Initialize(ctx,  cfg);
sc  .Initialize(dev, ctx, w, h, cfg);
pipe.Initialize(dev, sc, shaders, vertexInput);
cmd .Initialize(dev,  cfg);
sync.Initialize(dev,  cfg);
```

Notes:
- **Backward compatible.** Zero-arg `Initialize(...)` still works — that path
  is literally a `return Initialize(..., Config())`.
- **Present mode fallbacks.** `Mailbox` / `Immediate` fall back to `Fifo` if
  the driver doesn't expose them; `Auto` picks `Mailbox` when available,
  `Fifo` otherwise. `LogVk` reports the final choice.
- **MSAA (`cfg.swapchain.msaaSamples`) — reserved, clamped to 1x today.**
  The field + accessor exist so the API surface is stable, but proper MSAA
  needs a render-pass resolve attachment and a per-swapchain-image multi-
  sampled colour image — both are on the roadmap (see [Design](Design)).
  Setting `msaaSamples > 1` currently logs a warning and is clamped to
  `VK_SAMPLE_COUNT_1_BIT` in `VulkanPipeline`. Do not rely on it yet.
- **framesInFlight** is clamped to `[1, MAX_FRAMES_IN_FLIGHT]` (= 3). Going
  deeper requires `VK_KHR_timeline_semaphore` — a separate track; see
  `TimelineSemaphore` in [Expansion API](Expansion-API).
- **`cmd` and `sync` must share the same `framesInFlight`.** Pass the same
  `cfg` to both. The zero-arg path uses `2` for both, so it's always
  consistent by default.

### Raw-handle escape hatches

Every class also keeps a raw-handle overload so you can always drop down to
manual Vulkan for anything VCK is not doing for you. Preferred overloads are
literally a one-line forward to these:

```cpp
dev.Initialize (VkInstance, VkSurfaceKHR);
dev.Initialize (VkInstance, VkSurfaceKHR, const Config&);
sc .Initialize (VulkanDevice&, VkSurfaceKHR, w, h);
sc .Initialize (VulkanDevice&, VkSurfaceKHR, w, h, const Config&);
pipe.Initialize(VulkanDevice&, VkFormat, shaders, vi,
                VkSampleCountFlagBits = VK_SAMPLE_COUNT_1_BIT);
```

## Minimal use (zero-config)

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
[Execution Layer](Execution-Layer) — the core API never changes.
