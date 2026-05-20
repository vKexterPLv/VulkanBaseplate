<div align="center">

# Expansion API &nbsp;·&nbsp; `VCKExpansion.h`

Framebuffers · depth · samplers · textures · meshes · descriptors · mipmaps

</div>

---

Higher-level, non-owning building blocks composed on top of the core. Every
class here borrows core objects by reference. `VCKExpansion.cpp` is compiled
into every example (`VKB` in `build.bat`).

---

## VulkanOneTimeCommand

One-shot GPU command for setup work (staging copies, layout transitions).

```cpp
VCK::VulkanOneTimeCommand otc;
if (otc.Begin(dev, cmd))
{
    vkCmdCopyBuffer(otc.Cmd(), src, dst, 1, &region);
    otc.End();   // submit + vkQueueWaitIdle + free
}
```

Blocks on `vkQueueWaitIdle` — **not** for per-frame use.

## VulkanFramebufferSet

One `VkFramebuffer` per swapchain image.

```cpp
VCK::VulkanFramebufferSet fbs;
fbs.Initialize(dev, sc, pipeline);
// (with depth attachment)  fbs.Initialize(dev, sc, pipeline, depth);
// on resize:
fbs.Recreate(pipeline);
// (with depth)               fbs.Recreate(pipeline, depth);
// in DrawFrame():
rp.framebuffer = fbs.Get(imageIndex);
```

## VulkanDepthBuffer

```cpp
VCK::VulkanDepthBuffer depth;
depth.Initialize(dev, width, height);      // picks a supported D32/D24 format
// on resize:
depth.Recreate(width, height);
// pass to framebuffers:
fbs.Initialize(dev, sc, renderPass, depth.GetImageView());
```

## VulkanSampler

```cpp
VCK::VulkanSampler samp;
samp.CreateLinear(dev);         // trilinear, repeat, anisotropy if supported
// or samp.CreateNearest(dev);  // point sampling
VkSampler handle = samp.Get();
```

## VulkanTexture

```cpp
VCK::VulkanTexture tex;
tex.CreateFromPixels(dev, cmd,
                     pixels, width, height,
                     VK_FORMAT_R8G8B8A8_SRGB);
VkImageView view = tex.Image().GetView();
```

CPU → GPU upload with hidden staging buffer + layout transitions.

## VulkanMesh

```cpp
struct V { float pos[3]; float uv[2]; };
V        verts[3] = { /* ... */ };
uint32_t idx[3]   = { 0, 1, 2 };

VCK::VulkanMesh mesh;
mesh.Upload(dev, cmd,
            verts, sizeof(verts),
            idx,   3);

// inside a render pass:
mesh.RecordDraw(f.PrimaryCmd());
```

## VulkanDescriptorLayoutBuilder

```cpp
VkDescriptorSetLayout layout = VCK::VulkanDescriptorLayoutBuilder()
    .AddUniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
    .AddCombinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .Build(dev);
```

## VulkanDescriptorPool

```cpp
VCK::VulkanDescriptorPool pool;
pool.Initialize(dev, layout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                /*framesInFlight=*/cfg.sync.framesInFlight);
VkDescriptorSet set = pool.GetSet(frameSlot);   // one set per frame slot
```

The pool allocates exactly `framesInFlight` descriptor sets — pass the
same value you set on `VulkanSync` / `VulkanCommand` so the descriptor
ring matches the frame ring exactly.  `framesInFlight` must be in
`[1, MAX_FRAMES_IN_FLIGHT]`; out-of-range values return `false` and
emit `VCKLog::Error("DescriptorPool", ...)`.

## VulkanUniformSet\<T\>

Per-frame typed UBO.  Reads `pool.GetFramesInFlight()` on `Initialize`
and sizes its internal buffer ring to match — the user threads
`framesInFlight` to the pool only, never to the UBO.

```cpp
struct SceneUbo { glm::mat4 view, proj; };

VCK::VulkanUniformSet<SceneUbo> ubo;
ubo.Initialize(dev, pool, /*binding=*/0);

// per frame:
ubo.Write(frameSlot, { view, proj });
vkCmdBindDescriptorSets(f.PrimaryCmd(),
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipelineLayout, 0, 1,
                        &pool.GetSet(frameSlot),
                        0, nullptr);
```

## VulkanDescriptorAllocator

For when one pool / one type isn't enough.

```cpp
VCK::VulkanDescriptorAllocator alloc;
alloc.Initialize(dev, /*maxSets=*/32, {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         16 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
});
VkDescriptorSet set = alloc.Allocate(layout);
```

## VulkanModelPipeline

Graphics pipeline tailored for a single mesh + UBO + texture.

```cpp
VCK::VulkanModelPipeline modelPipeline;
modelPipeline.Initialize(dev, pipeline.GetRenderPass(), shaders, vertexInput);
// draw:
vkCmdBindPipeline(f.PrimaryCmd(),
                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                  modelPipeline.GetPipeline());
```

## VulkanMipmapGenerator

```cpp
uint32_t mips = VCK::VulkanMipmapGenerator::MipLevels(w, h);
if (VCK::VulkanMipmapGenerator::IsFormatSupported(dev, VK_FORMAT_R8G8B8A8_SRGB))
{
    VCK::VulkanMipmapGenerator gen;
    gen.Generate(dev, cmd, image, w, h, mips);
}
```

The source image must have been created with `TRANSFER_SRC_BIT | TRANSFER_DST_BIT`
usage.

---

## v0.5 Classes

### HotReload [31]

Drives the shader hot-reload pipeline: `ShaderWatcher` detects a `.spv`
change, `DrainInFlight` retires in-flight frames, then `Reinitialize` +
`Recreate` rebuild the pipeline and framebuffers in place.

```cpp
VCK::HotReload hotReload;
hotReload.Initialize(device, watcher, scheduler, pipeline, swapchain, framebuffers, shaders, vertexInput);
// Run on a background thread or poll in the main loop:
hotReload.Poll();
hotReload.Shutdown();
```

Enabled only when `cfg.debug = true`. In release builds `Poll()` is a no-op
and `ShaderWatcher` emits a `VCKLog::Notice` if instantiated outside debug mode.

---

### OffscreenTarget [32]

Allocates a `VulkanImage` + matching `VkRenderPass` + `VkFramebuffer` for
render-to-texture. The image format and extent are caller-specified.

```cpp
VCK::OffscreenTarget offscreen;
offscreen.Initialize(device, VK_FORMAT_R8G8B8A8_UNORM, {width, height});

// In your draw loop — pass 1:
VkRenderPassBeginInfo rp{};
rp.renderPass  = offscreen.GetRenderPass();
rp.framebuffer = offscreen.GetFramebuffer();
rp.renderArea.extent = offscreen.GetExtent();
vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
// ... draw scene ...
vkCmdEndRenderPass(cmd);

offscreen.Shutdown();
```

Accessors: `GetRenderPass()`, `GetFramebuffer()`, `GetImageView()`, `GetExtent()`.

---

### FullscreenPass [33]

A fullscreen triangle that samples one `COMBINED_IMAGE_SAMPLER` input and
writes to the swapchain colour attachment. Pair with `OffscreenTarget` for a
two-pass blit.

```cpp
VCK::FullscreenPass fsPass;
fsPass.Initialize(device, swapchain, offscreen.GetImageView(), sampler);

// In your draw loop — pass 2:
fsPass.Record(cmd, framebuffers.Get(imageIndex), swapchain.GetExtent());

fsPass.Shutdown();
```

`Initialize` creates its own render pass (swapchain format) and a descriptor
set that holds the input image view + sampler. `Record` begins the render
pass, binds the fullscreen pipeline, issues one `vkCmdDraw(3, 1, 0, 0)`, and
ends the render pass. No vertex buffer needed.

---

### InitializeBindless / WriteBindless (VulkanDescriptorAllocator)

`VulkanDescriptorAllocator` gained two methods in v0.5 for bindless descriptor
indexing (requires `cfg.device.enableBindless = true`):

```cpp
VCK::VulkanDescriptorAllocator allocator;

// Create a bindless pool for up to 1024 combined-image-sampler slots:
allocator.InitializeBindless(device,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024);

// Fill slot 0:
allocator.WriteBindless(0, texture.GetImageView(), sampler.GetSampler());

// Bind in your draw loop:
VkDescriptorSet bindlessSet = allocator.GetBindlessSet();
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipeLayout, 0, 1, &bindlessSet, 0, nullptr);

// Push the index:
uint32_t idx = 0;
vkCmdPushConstants(cmd, pipeLayout,
    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(idx), &idx);

allocator.Shutdown();
```

The descriptor set layout uses `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`. Slots not filled via
`WriteBindless` are left unbound — reading them from a shader is undefined
unless `GL_EXT_nonuniform_qualifier` guards the access.
