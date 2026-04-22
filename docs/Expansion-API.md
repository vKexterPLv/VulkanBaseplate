# Expansion API — `VCKExpansion.h`

Higher-level, non-owning building blocks composed on top of the core. Every
class here borrows core objects by reference. `VCKExpansion.cpp` is compiled
into every example (`VKB` in `build.bat`).

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
pool.Initialize(dev, layout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
VkDescriptorSet set = pool.GetSet(frameSlot);   // one set per frame slot
```

## VulkanUniformSet\<T\>

Per-frame typed UBO. Owns up to `MAX_FRAMES_IN_FLIGHT` (= 3) buffers —
iterates only over the runtime `framesInFlight` you set on `VulkanSync` /
`VulkanCommand`, and points the pool's descriptor sets at them on
`Initialize`.

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
