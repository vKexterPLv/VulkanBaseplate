<div align="center">

# Cookbook

Copy-paste recipes for things the kit doesn't ship but every Vulkan app ends up needing.

</div>

---

Every recipe assumes you already have a running VCK app — a `VulkanContext`,
`VulkanDevice`, `VulkanSwapchain`, `VulkanSync`, `VulkanCommand`, and a
draw loop (copy any of the 13 examples). Snippets below slot **into** that
app; they are not standalone programs.

Each recipe calls out:

- **Needs** — extra headers or third-party bits you must drop into `vendor/`
- **Rules** — which of the [22 design rules](Design) the recipe respects
- **Shader** — GLSL when it's more than ten lines, inline otherwise
- **C++** — the VCK-side wiring

> None of these recipes live in `VCK.h`. Rule 15 (minimal core surface) and
> rule 21 (`VCK.h` is the API surface) keep the kit small; cookbook code
> stays in your app.

---

## Table of contents

**Asset loading**
1. [Image loading](#1-image-loading-stb_image--vulkanimage)
2. [OBJ loading](#2-obj-loading-tinyobjloader--vulkanbuffer)

**Primitives**
3. [Cube rendering](#3-cube-rendering-indexed-mesh--push-constants)
4. [Line drawing](#4-line-drawing-dynamic-vertex-buffer)
5. [Circle / SDF shapes](#5-circle--sdf-shapes-fullscreen-frag-shader)
6. [Text rendering](#6-text-rendering-bitmap-font-atlas)

**Post-process anti-aliasing** &nbsp;*(complements the sample-based AA wired by VCK)*
7. [FXAA 3.11](#7-fxaa-311-single-pass)
8. [SMAA 1x skeleton](#8-smaa-1x-skeleton-three-pass)
9. [TAA skeleton](#9-taa-skeleton-history--reprojection)

**From Devin (picked because people always end up building them)**
10. [ImGui bootstrap](#10-imgui-bootstrap)
11. [Offscreen render + PNG readback](#11-offscreen-render--png-readback)

---

## 1. Image loading (`stb_image` → `VulkanImage`)

**Needs.** Drop [`stb_image.h`](https://github.com/nothings/stb/blob/master/stb_image.h)
into `vendor/stb/stb_image.h`. Header-only, public domain.

**Rules.** R9 (escape hatch — returns the raw `VkImage`), R14 (fail loud on
`stbi_load` failure), R22 (the user owns the returned `VulkanImage`).

```cpp
// Somewhere in your .cpp:
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

bool LoadImage2D(VulkanDevice&        device,
                 VulkanCommand&       cmd,      // for a one-time staging copy
                 const char*          path,
                 VulkanImage&         outImage, // caller-owned
                 VulkanBuffer*        staging)  // optional; recycled if provided
{
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(path, &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) {
        VCKLog::Error("LoadImage2D",
            std::string("stbi_load failed: ") + stbi_failure_reason());
        return false;
    }

    const VkDeviceSize bytes = VkDeviceSize(w) * h * 4;

    VulkanBuffer localStaging;
    VulkanBuffer& stg = staging ? *staging : localStaging;
    if (!stg.Initialize(device, bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        stbi_image_free(pixels);
        return false;
    }
    stg.Upload(pixels, bytes);
    stbi_image_free(pixels);

    if (!outImage.Initialize(device,
            uint32_t(w), uint32_t(h),
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        return false;
    }

    VulkanOneTimeCommand one(device, cmd.GetPool());
    outImage.TransitionLayout(one.Get(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    outImage.CopyFromBuffer(one.Get(), stg.Get(), uint32_t(w), uint32_t(h));
    outImage.TransitionLayout(one.Get(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    one.End();   // rule 4: blocks via vkQueueWaitIdle — setup only

    if (!staging) localStaging.Shutdown();
    return true;
}
```

Pair with a `VulkanSampler` using `MipmapExample`'s pattern if you need
mips. For mipmapped textures, generate mips on the GPU with
`vkCmdBlitImage` rather than CPU-side — see
[`MipmapExample/App.cpp`](../tree/VCK/example/MipmapExample/App.cpp) for the
full blit chain.

---

## 2. OBJ loading (`tinyobjloader` → `VulkanBuffer`)

**Needs.** Drop [`tiny_obj_loader.h`](https://github.com/tinyobjloader/tinyobjloader/blob/release/tiny_obj_loader.h)
into `vendor/tinyobjloader/tiny_obj_loader.h`. Header-only, MIT.

**Rules.** R1 (vertex layout is explicit in the struct), R14 (fail loud on
parse error), R22 (caller owns the buffers).

```cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tinyobjloader/tiny_obj_loader.h"

struct ObjVertex {
    float position[3];
    float normal  [3];
    float uv      [2];
};

bool LoadOBJ(VulkanDevice&              device,
             const char*                path,
             std::vector<ObjVertex>&    vertices,
             std::vector<uint32_t>&     indices)
{
    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path)) {
        VCKLog::Error("LoadOBJ", err.empty() ? "parse failed" : err);
        return false;
    }
    if (!warn.empty()) VCKLog::Warn("LoadOBJ", warn);

    // De-duplicate (pos, normal, uv) tuples so the index buffer actually
    // compresses the mesh.
    std::unordered_map<uint64_t, uint32_t> uniq;
    uniq.reserve(attrib.vertices.size());

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            ObjVertex v{};
            v.position[0] = attrib.vertices[3 * idx.vertex_index + 0];
            v.position[1] = attrib.vertices[3 * idx.vertex_index + 1];
            v.position[2] = attrib.vertices[3 * idx.vertex_index + 2];
            if (idx.normal_index >= 0) {
                v.normal[0] = attrib.normals[3 * idx.normal_index + 0];
                v.normal[1] = attrib.normals[3 * idx.normal_index + 1];
                v.normal[2] = attrib.normals[3 * idx.normal_index + 2];
            }
            if (idx.texcoord_index >= 0) {
                v.uv[0] =        attrib.texcoords[2 * idx.texcoord_index + 0];
                v.uv[1] = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
            }

            // Cheap 64-bit hash of the three indices is fine for dedup.
            uint64_t key =
                (uint64_t(idx.vertex_index)   << 42) ^
                (uint64_t(idx.normal_index)   << 21) ^
                 uint64_t(idx.texcoord_index);

            auto it = uniq.find(key);
            if (it == uniq.end()) {
                uint32_t newIdx = uint32_t(vertices.size());
                vertices.push_back(v);
                indices .push_back(newIdx);
                uniq.emplace(key, newIdx);
            } else {
                indices.push_back(it->second);
            }
        }
    }

    VCKLog::Notice("LoadOBJ",
        std::to_string(vertices.size()) + " unique verts, " +
        std::to_string(indices .size()) + " indices");
    return true;
}
```

Then upload `vertices` into a `VulkanBuffer` with `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`
and `indices` into one with `VK_BUFFER_USAGE_INDEX_BUFFER_BIT` exactly like
`VMMExample` does.

---

## 3. Cube rendering (indexed mesh + push constants)

**Rules.** R1 (vertex layout explicit in CPU + shader), R17 (all GPU work
bound to the frame), R22 (mesh buffers caller-owned).

**`cube.vert`:**

```glsl
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;

layout(push_constant) uniform PC {
    mat4 mvp;   // precomputed CPU-side: proj * view * model
    mat4 model; // for lighting in world space
} pc;

layout(location = 0) out vec3 vNrm;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNrm        = normalize(mat3(pc.model) * inNrm);
}
```

**`cube.frag`:**

```glsl
#version 450

layout(location = 0) in  vec3 vNrm;
layout(location = 0) out vec4 outColor;

void main() {
    // Single directional light straight down-right.
    vec3  L   = normalize(vec3(0.4, -1.0, 0.3));
    float nl  = max(dot(vNrm, -L), 0.0);
    vec3  col = mix(vec3(0.08), vec3(0.9, 0.6, 0.2), nl);
    outColor  = vec4(col, 1.0);
}
```

**C++ vertex data (24 verts, 36 indices):**

```cpp
struct CubeVtx { float pos[3]; float nrm[3]; };

// 6 faces × 4 verts each, per-face normal so flat shading works.
static const CubeVtx kCubeVerts[24] = {
    // +X
    {{ 0.5, -0.5, -0.5}, {1,0,0}}, {{ 0.5,  0.5, -0.5}, {1,0,0}},
    {{ 0.5,  0.5,  0.5}, {1,0,0}}, {{ 0.5, -0.5,  0.5}, {1,0,0}},
    // -X
    {{-0.5, -0.5,  0.5},{-1,0,0}}, {{-0.5,  0.5,  0.5},{-1,0,0}},
    {{-0.5,  0.5, -0.5},{-1,0,0}}, {{-0.5, -0.5, -0.5},{-1,0,0}},
    // +Y
    {{-0.5,  0.5, -0.5},{0,1,0}},  {{-0.5,  0.5,  0.5},{0,1,0}},
    {{ 0.5,  0.5,  0.5},{0,1,0}},  {{ 0.5,  0.5, -0.5},{0,1,0}},
    // -Y
    {{-0.5, -0.5,  0.5},{0,-1,0}}, {{-0.5, -0.5, -0.5},{0,-1,0}},
    {{ 0.5, -0.5, -0.5},{0,-1,0}}, {{ 0.5, -0.5,  0.5},{0,-1,0}},
    // +Z
    {{-0.5, -0.5,  0.5},{0,0,1}},  {{ 0.5, -0.5,  0.5},{0,0,1}},
    {{ 0.5,  0.5,  0.5},{0,0,1}},  {{-0.5,  0.5,  0.5},{0,0,1}},
    // -Z
    {{ 0.5, -0.5, -0.5},{0,0,-1}}, {{-0.5, -0.5, -0.5},{0,0,-1}},
    {{-0.5,  0.5, -0.5},{0,0,-1}}, {{ 0.5,  0.5, -0.5},{0,0,-1}},
};

static const uint16_t kCubeIdx[36] = {
     0, 1, 2,  0, 2, 3,       4, 5, 6,  4, 6, 7,
     8, 9,10,  8,10,11,      12,13,14, 12,14,15,
    16,17,18, 16,18,19,      20,21,22, 20,22,23,
};

// In DrawFrame (per-frame push-constant upload):
struct PC { float mvp[16]; float model[16]; };
PC pc{};
/* fill pc.mvp = proj * view * model; pc.model = model */
vkCmdPushConstants(cb, pipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PC), &pc);
vkCmdBindIndexBuffer(cb, indexBuf.Get(), 0, VK_INDEX_TYPE_UINT16);
vkCmdDrawIndexed(cb, 36, 1, 0, 0, 0);
```

---

## 4. Line drawing (dynamic vertex buffer)

**Rules.** R5 (vertex buffer is frame-scoped or persistent, your call), R9
(raw `vkCmdSetLineWidth` is the escape hatch).

**`line.vert`:**

```glsl
#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(push_constant) uniform PC { mat4 vp; } pc;
layout(location = 0) out vec3 vColor;
void main() {
    gl_Position = pc.vp * vec4(inPos, 1.0);
    vColor      = inColor;
}
```

**`line.frag`:** 4 lines, same `outColor = vec4(vColor, 1.0);` pattern as
the triangle.

**Pipeline tweaks:**

```cpp
VkPipelineInputAssemblyStateCreateInfo ia{};
ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

VkPipelineRasterizationStateCreateInfo rs{};
rs.sType     = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
rs.polygonMode = VK_POLYGON_MODE_LINE;
rs.lineWidth   = 1.0f;  // use VK_DYNAMIC_STATE_LINE_WIDTH + vkCmdSetLineWidth
                        // for runtime changes.  Check device.GetLimits()
                        // .lineWidthRange - only { 1.0, 1.0 } is guaranteed.
```

**Per-frame line-batch upload:**

```cpp
struct Vert { float pos[3]; float col[3]; };
std::vector<Vert> lines;
auto AddLine = [&](const float a[3], const float b[3], const float c[3]) {
    lines.push_back({{a[0],a[1],a[2]}, {c[0],c[1],c[2]}});
    lines.push_back({{b[0],b[1],b[2]}, {c[0],c[1],c[2]}});
};
// AddLine(...) for every line you want this frame, then:
lineBuf.Upload(lines.data(), lines.size() * sizeof(Vert));
vkCmdBindVertexBuffers(cb, 0, 1, &lineBuf.Get(), &offset);
vkCmdDraw(cb, uint32_t(lines.size()), 1, 0, 0);
```

For *anti-aliased* lines wider than `lineWidthRange.y`, render each line as
a screen-space quad in the vertex shader (extrude along perpendicular in
clip space, feather in the fragment shader). That pattern is also the
base for debug-axis gizmos.

---

## 5. Circle / SDF shapes (fullscreen frag shader)

**Rules.** R19 (zero geometry cost — just a fullscreen triangle), R1
(parameters passed as push constants, not baked into the shader).

**`sdf.vert`** (fullscreen triangle — **no vertex buffer needed**):

```glsl
#version 450
layout(location = 0) out vec2 vUV;
void main() {
    // Three verts that cover the whole screen (Sascha Willems trick):
    vUV         = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
```

**`sdf.frag`** (anti-aliased circle, disc, ring):

```glsl
#version 450
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec2  center;       // in UV space, [0, 1]
    float radius;       // same
    float ringThick;    // >0 = ring (annulus); 0 = filled disc
    vec4  color;
} pc;

float aaStep(float edge, float x) {
    float w = fwidth(x) * 0.7;   // auto-AA width in pixels
    return smoothstep(edge - w, edge + w, x);
}

void main() {
    float d = distance(vUV, pc.center);           // distance to centre
    float a;
    if (pc.ringThick > 0.0) {
        // Annulus: inside outer edge AND outside inner edge.
        float outer = 1.0 - aaStep(pc.radius,                      d);
        float inner =       aaStep(pc.radius - pc.ringThick,       d);
        a = outer * inner;
    } else {
        a = 1.0 - aaStep(pc.radius, d);            // filled disc
    }
    outColor = vec4(pc.color.rgb, pc.color.a * a);
}
```

Draw call: `vkCmdDraw(cb, 3, 1, 0, 0);` — no vertex buffer, no index buffer.

Same SDF pattern works for rectangles (`sdBox`), rounded rectangles, hearts,
stars — just swap the distance function. [Inigo Quilez's primitives
list](https://iquilezles.org/articles/distfunctions2d/) is the reference.

---

## 6. Text rendering (bitmap font atlas)

**Needs.** A pre-rendered atlas PNG + a JSON/CSV sidecar with glyph UVs.
Generate with [msdf-atlas-gen](https://github.com/Chlumsky/msdf-atlas-gen)
or the older [BMFont](https://www.angelcode.com/products/bmfont/). Both
export the same conceptual format: `{ char, u, v, w, h, xoff, yoff, xadvance }`.

**Rules.** R5 (per-frame vertex buffer is frame-scoped), R20 (pair with a
`TextExample` if you promote this to VCK).

**`text.vert`:**

```glsl
#version 450
layout(location = 0) in vec2 inPos;    // pixel-space, [0, screen]
layout(location = 1) in vec2 inUV;
layout(push_constant) uniform PC { vec2 screen; } pc;
layout(location = 0) out vec2 vUV;
void main() {
    vec2 ndc    = (inPos / pc.screen) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV         = inUV;
}
```

**`text.frag`** (straight bitmap — 4 lines):

```glsl
#version 450
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uFont;
void main() {
    float a  = texture(uFont, vUV).r;   // single-channel atlas
    outColor = vec4(1.0, 1.0, 1.0, a);
}
```

**C++ glyph batching:**

```cpp
struct Glyph { float u, v, w, h, xoff, yoff, xadvance; };
std::unordered_map<char, Glyph> glyphs;  // populated from your sidecar

struct TextVtx { float pos[2]; float uv[2]; };

void EmitString(std::vector<TextVtx>& out,
                const std::string&    s,
                float cursorX, float cursorY,
                float atlasW,  float atlasH)
{
    for (char c : s) {
        auto it = glyphs.find(c);
        if (it == glyphs.end()) continue;
        const Glyph& g = it->second;

        float x0 = cursorX + g.xoff, y0 = cursorY + g.yoff;
        float x1 = x0 + g.w,         y1 = y0 + g.h;
        float u0 = g.u / atlasW,     v0 = g.v / atlasH;
        float u1 = (g.u + g.w) / atlasW, v1 = (g.v + g.h) / atlasH;

        // Two triangles per glyph.
        out.push_back({{x0, y0}, {u0, v0}});
        out.push_back({{x1, y0}, {u1, v0}});
        out.push_back({{x1, y1}, {u1, v1}});
        out.push_back({{x0, y0}, {u0, v0}});
        out.push_back({{x1, y1}, {u1, v1}});
        out.push_back({{x0, y1}, {u0, v1}});

        cursorX += g.xadvance;
    }
}
```

Upload into a frame-scoped `VulkanBuffer`, bind, draw. For **MSDF** (crisp
at any zoom), swap the frag shader for the standard
`median(msdf.r, msdf.g, msdf.b)` + `smoothstep` trick — Chlumsky's readme
has the exact lines.

---

## 7. FXAA 3.11 (single-pass)

**Rules.** R15 (lives in your renderer, not `VCK.h`), R9 (reads the raw
swapchain image via `GetImageView()`), R13 (cheap enough to ship always-on
for debug builds).

FXAA is one fragment shader over a fullscreen triangle (recipe 5). VCK
already detects post-process AA paths — when `DetectRecommendedAA` returns
`AATechnique::FXAA`, your renderer runs this pass *after* the main render
pass and *before* present.

**`fxaa.frag`** (condensed from Timothy Lottes' FXAA 3.11 PC quality preset):

```glsl
#version 450
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uSrc;
layout(push_constant) uniform PC { vec2 rcpFrame; } pc;   // 1.0 / resolution

#define FXAA_SPAN_MAX      8.0
#define FXAA_REDUCE_MUL    (1.0 / 8.0)
#define FXAA_REDUCE_MIN    (1.0 / 128.0)

void main() {
    vec3 rgbNW = texture(uSrc, vUV + vec2(-1.0, -1.0) * pc.rcpFrame).rgb;
    vec3 rgbNE = texture(uSrc, vUV + vec2( 1.0, -1.0) * pc.rcpFrame).rgb;
    vec3 rgbSW = texture(uSrc, vUV + vec2(-1.0,  1.0) * pc.rcpFrame).rgb;
    vec3 rgbSE = texture(uSrc, vUV + vec2( 1.0,  1.0) * pc.rcpFrame).rgb;
    vec3 rgbM  = texture(uSrc, vUV).rgb;

    vec3 luma  = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma), lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma), lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM , luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * FXAA_REDUCE_MUL,
                          FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * pc.rcpFrame;

    vec3 rgbA = 0.5 * (texture(uSrc, vUV + dir * (1.0/3.0 - 0.5)).rgb +
                       texture(uSrc, vUV + dir * (2.0/3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uSrc, vUV + dir * -0.5).rgb +
                                     texture(uSrc, vUV + dir *  0.5).rgb);
    float lumaB = dot(rgbB, luma);
    outColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
```

**C++ wiring.** Same as recipe 5 (fullscreen triangle) — the only new thing
is the descriptor set binding your scene colour texture and sending
`vec2(1.0f/width, 1.0f/height)` as push constants.

---

## 8. SMAA 1x skeleton (three-pass)

**Rules.** R12 (each of the three passes is an explicit render-pass event).

SMAA 1x is three passes over the scene colour:

1. **Edge detection** — `SMAA_EdgeDetectionPS` (luma or colour); outputs
   a 2-channel R8G8 edge texture.
2. **Blend-weight calculation** — `SMAA_BlendingWeightCalculationPS`;
   samples precomputed `AreaTex` + `SearchTex` to produce per-pixel
   blend weights.
3. **Neighbourhood blending** — `SMAA_NeighborhoodBlendingPS`; blends
   the scene colour using the weights.

The shader is too long to inline. Grab the official header from
[iryoku/smaa](https://github.com/iryoku/smaa/blob/master/SMAA.hlsl)
and the two precomputed textures (`AreaTex.dds`, `SearchTex.dds`) from
the same repo. Convert them to `.ktx` or decode to raw at startup and
upload via recipe 1. A minimal VCK driver:

```cpp
// Resources (persistent):
VulkanImage edges, weights;              // 2 intermediate RTs, matching swapchain extent
VulkanImage areaTex, searchTex;          // loaded once at boot
VulkanPipeline edgePipe, weightPipe, blendPipe;

// Per-frame:
void RunSMAA1x(VkCommandBuffer cb, VulkanImage& sceneColor, VulkanImage& dst) {
    // Pass 1: edge detection
    BeginRenderPass(cb, edges);
    BindPipeline(cb, edgePipe);
    BindTexture (cb, 0, sceneColor);
    vkCmdDraw(cb, 3, 1, 0, 0);
    EndRenderPass(cb);

    // Pass 2: blend weights
    BeginRenderPass(cb, weights);
    BindPipeline(cb, weightPipe);
    BindTextures(cb, { edges, areaTex, searchTex });
    vkCmdDraw(cb, 3, 1, 0, 0);
    EndRenderPass(cb);

    // Pass 3: neighbourhood blending
    BeginRenderPass(cb, dst);
    BindPipeline(cb, blendPipe);
    BindTextures(cb, { sceneColor, weights });
    vkCmdDraw(cb, 3, 1, 0, 0);
    EndRenderPass(cb);
}
```

When `swapchain.GetAATechnique() == AATechnique::SMAA_1x`, your renderer
enables this path. VCK does not touch it — rule 15.

---

## 9. TAA skeleton (history + reprojection)

**Rules.** R11 (opt-in nondeterminism — TAA uses jitter, that's fine per
the rule), R17 (history is a persistent resource, not frame-scoped).

TAA needs four things VCK already exposes:

1. A **sub-pixel jitter** on the projection matrix, rotating per frame
   (8-tap Halton works). Jitter offset goes through the push constants.
2. A **velocity render target** (`R16G16_SFLOAT`), written per-pixel as
   `(currentClipPos - previousClipPos).xy` in the main pass.
3. A **history colour buffer** (same format as swapchain), persistent,
   recreated alongside the swapchain.
4. A resolve pass that reads `sceneColor`, `history`, `velocity` and
   outputs the final colour.

**`taa_resolve.frag`** (neighbourhood-clamp variant, ~30 lines):

```glsl
#version 450
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uHistory;
layout(set = 0, binding = 2) uniform sampler2D uVelocity;

layout(push_constant) uniform PC {
    vec2  rcpFrame;
    float blendWeight;   // ~0.1; lower = crisper, higher = smoother
} pc;

void main() {
    vec2 v       = texture(uVelocity, vUV).xy;
    vec2 prevUV  = vUV - v;
    vec3 current = texture(uScene,   vUV   ).rgb;
    vec3 history = texture(uHistory, prevUV).rgb;

    // Neighbourhood clamp: reject history that falls outside a 3x3
    // box of the current neighbourhood (ghosting fix).
    vec3 aabbMin =  vec3( 9999.0);
    vec3 aabbMax = -vec3( 9999.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec3 s = texture(uScene, vUV + vec2(x, y) * pc.rcpFrame).rgb;
            aabbMin = min(aabbMin, s);
            aabbMax = max(aabbMax, s);
        }
    history = clamp(history, aabbMin, aabbMax);

    vec3 resolved = mix(history, current, pc.blendWeight);
    outColor      = vec4(resolved, 1.0);
}
```

**Two-history ping-pong:** keep two colour targets, read from one, write
to the other; swap each frame. On swapchain recreate (rule 12), **clear
both histories** — stale pixels at the wrong resolution ghost horribly.

**TAAU:** identical pass with `uScene` at a lower resolution and the
output at full resolution. Upscale in the resolve.

---

## 10. ImGui bootstrap

**Needs.** Drop `imgui/` from the [ocornut/imgui](https://github.com/ocornut/imgui)
master branch into `vendor/imgui/`. You want the core files +
`backends/imgui_impl_glfw.cpp` + `backends/imgui_impl_vulkan.cpp`.

**Rules.** R9 (VCK exposes everything ImGui's backend needs: instance,
physical device, device, queue, command pool), R19 (zero cost if you
never call `Init`), R22 (ImGui owns its own descriptor pool — VCK does
not track it).

**Compile.** Add these to your `build.bat` / `build.sh` compile step:

```
../vendor/imgui/imgui.cpp
../vendor/imgui/imgui_draw.cpp
../vendor/imgui/imgui_tables.cpp
../vendor/imgui/imgui_widgets.cpp
../vendor/imgui/backends/imgui_impl_glfw.cpp
../vendor/imgui/backends/imgui_impl_vulkan.cpp
```

**C++ (drop into an existing VCK app):**

```cpp
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"

VkDescriptorPool gImguiPool = VK_NULL_HANDLE;   // ImGui-owned, not VCK-owned

void InitImGui(VulkanContext&   ctx,
               VulkanDevice&    dev,
               VulkanSwapchain& sc,
               VulkanPipeline&  pipe,    // for the render pass only
               VCK::Window&     window)
{
    // --- 1. ImGui-owned descriptor pool (rule 22 — VCK does not destroy it)
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER,                1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000},
    };
    VkDescriptorPoolCreateInfo pci{};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets       = 1000 * uint32_t(sizeof(sizes) / sizeof(sizes[0]));
    pci.poolSizeCount = uint32_t(sizeof(sizes) / sizeof(sizes[0]));
    pci.pPoolSizes    = sizes;
    VK_CHECK(vkCreateDescriptorPool(dev.Get(), &pci, nullptr, &gImguiPool));

    // --- 2. ImGui state
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window.GetHandle(), true);   // VCK::Window exposes raw GLFW handle

    ImGui_ImplVulkan_InitInfo vi{};
    vi.Instance       = ctx.GetInstance();
    vi.PhysicalDevice = dev.GetPhysicalDevice();
    vi.Device         = dev.Get();
    vi.Queue          = dev.GetGraphicsQueue();
    vi.QueueFamily    = dev.GetGraphicsFamily();
    vi.DescriptorPool = gImguiPool;
    vi.MinImageCount  = sc.GetImageCount();
    vi.ImageCount     = sc.GetImageCount();
    vi.MSAASamples    = sc.GetMSAASamples();  // rule: match the render pass
    ImGui_ImplVulkan_Init(&vi, pipe.GetRenderPass());

    // --- 3. Upload ImGui's font atlas via a one-time command
    VulkanOneTimeCommand one(dev, cmd.GetPool());
    ImGui_ImplVulkan_CreateFontsTexture(one.Get());
    one.End();
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

// Each frame, inside your render pass:
void DrawImGui(VkCommandBuffer cb) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("VCK");
    ImGui::Text("Frame time: %.2f ms", 1000.0f / ImGui::GetIO().Framerate);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
}

// On shutdown:
void ShutdownImGui(VulkanDevice& dev) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(dev.Get(), gImguiPool, nullptr);
    gImguiPool = VK_NULL_HANDLE;
}
```

On swapchain recreate, call `ImGui_ImplVulkan_SetMinImageCount(sc.GetImageCount())`.

---

## 11. Offscreen render + PNG readback

**Needs.** `stb_image_write.h` ([same repo as stb_image](https://github.com/nothings/stb/blob/master/stb_image_write.h)).

**Rules.** R14 (fail loud if readback copy fails), R19 (zero cost unless
you call it), R9 (returns the raw pixel buffer for any consumer).

Useful for: golden-image regression tests, screenshots, turning VCK
examples into headless CI artefacts.

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

bool RenderToPNG(VulkanDevice&  device,
                 VulkanCommand& cmd,
                 VulkanImage&   sourceImage,   // must have TRANSFER_SRC usage
                 uint32_t       w, uint32_t h,
                 const char*    outPath)
{
    const VkDeviceSize bytes = VkDeviceSize(w) * h * 4;

    VulkanBuffer readback;
    if (!readback.Initialize(device, bytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VCKLog::Error("RenderToPNG", "readback buffer alloc failed");
        return false;
    }

    VulkanOneTimeCommand one(device, cmd.GetPool());

    // Transition source to TRANSFER_SRC_OPTIMAL if it's currently SHADER_READ.
    sourceImage.TransitionLayout(one.Get(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = { w, h, 1 };
    vkCmdCopyImageToBuffer(one.Get(),
        sourceImage.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readback.Get(), 1, &region);

    // Put source back where the app expects it.
    sourceImage.TransitionLayout(one.Get(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    one.End();   // rule 4: blocks via vkQueueWaitIdle — offline tool, acceptable

    // Map + flip vertically (Vulkan = top-left origin; PNG expects same, but
    // framebuffers often come out bottom-up depending on projection).
    void* data = nullptr;
    VK_CHECK(vkMapMemory(device.Get(), readback.GetMemory(), 0, bytes, 0, &data));
    std::vector<uint8_t> rgba(bytes);
    std::memcpy(rgba.data(), data, bytes);
    vkUnmapMemory(device.Get(), readback.GetMemory());

    int ok = stbi_write_png(outPath, int(w), int(h), 4, rgba.data(), int(w) * 4);
    readback.Shutdown();

    if (!ok) {
        VCKLog::Error("RenderToPNG", std::string("stbi_write_png failed: ") + outPath);
        return false;
    }
    VCKLog::Notice("RenderToPNG",
        std::string(outPath) + " (" + std::to_string(w) + "x" + std::to_string(h) + ")");
    return true;
}
```

Use pattern:

```cpp
// After drawing the frame, before present (or to an offscreen target):
RenderToPNG(device, cmd, swapchain.GetColorImage(frameIdx),
            swapchain.GetExtent().width, swapchain.GetExtent().height,
            "frame_0001.png");
```

For **golden-image regression tests**, render at a fixed seed, compare the
output against a committed reference PNG pixel-for-pixel (allowing ±1 LSB
slack for floating-point drift). Flake-free across GPU vendors when the
seed is deterministic.

---

## Contributing recipes

Open a PR against `docs/Cookbook.md`. Keep each recipe to:

- One-line **intent** at the top.
- **Needs** block listing third-party headers / assets.
- **Rules** line listing which of the 22 rules apply.
- **Shader** (GLSL) if > 10 lines; inline otherwise.
- **C++** that slots into an existing VCK app.
- A closing note on where to go next.

Recipe length: aim for ≤ 150 lines. If it's longer, it probably wants to
be an example instead — see rule 20.
