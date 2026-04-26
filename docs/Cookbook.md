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

**Compute & GPU-driven**
12. [Compute dispatch](#12-compute-dispatch-queuesetcompute--queuecompute)
13. [GPU particle system](#13-gpu-particle-system-compute-sim--indirect-draw)
14. [Indirect draw](#14-indirect-draw-vkcmddrawindexedindirect)
15. [Async compute pattern](#15-async-compute-pattern-graphics--compute-overlap)

**Lighting & shading**
16. [Shadow mapping](#16-shadow-mapping-directional-light--pcf)
17. [Skybox / cubemap](#17-skybox--cubemap-rendering)
18. [PBR Cook-Torrance + IBL skeleton](#18-pbr-cook-torrance--ibl-skeleton)
19. [Deferred shading skeleton](#19-deferred-shading-skeleton-g-buffer--lighting)

**Post-process FX**
20. [HDR tonemapping](#20-hdr-tonemapping-reinhard--aces)
21. [Bloom](#21-bloom-bright-pass--mip-blur--composite)

**Dev experience & tooling**
22. [Shader hot-reload](#22-shader-hot-reload-watch--rebuild--swap)
23. [GPU picking](#23-gpu-picking-object-id-readback)
24. [Frustum culling](#24-frustum-culling-cpu-plane-test-vs-aabb)

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

## 12. Compute dispatch (`QueueSet::Compute()` + `QueueCompute`)

**Intent.** Run a compute pass that reads/writes a storage buffer or image,
either inline on the graphics queue or async on the dedicated compute queue
(v0.3 dedicated-queue support — rule 18 thread-safety applies).

**Rules.** R1 (you write the dispatch — VCK never auto-dispatches), R4
(no hidden sync; staging fences only), R18 (caller serialises if multiple
threads submit to the same `VkQueue`), R19 (zero cost when you don't use it).

**Shader** (`pass.comp`):
```glsl
#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0, std430) buffer Data { float v[]; } data;
layout(push_constant) uniform PC { uint count; float scale; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    data.v[i] = data.v[i] * pc.scale;
}
```

**C++** (compile to `pass.comp.spv`, then):
```cpp
// One-time setup:
VulkanPipeline computePipe;                              // compute variant
computePipe.InitializeCompute(device, "pass.comp.spv",
                              descriptorSetLayout, pushConstantRange);

// Per dispatch:
VkCommandBuffer cb = oneTime.Get();                      // or a frame primary
vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipe.Get());
vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                        computePipe.Layout(), 0, 1, &set, 0, nullptr);

struct PC { uint32_t count; float scale; } pc{N, 0.5f};
vkCmdPushConstants(cb, computePipe.Layout(),
                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

vkCmdDispatch(cb, (N + 255) / 256, 1, 1);

// If you write a storage buffer that the next graphics pass reads, insert
// a SHADER_WRITE -> SHADER_READ memory barrier (or an ACQUIRE barrier when
// crossing queue families — see VMM in v0.3 for the pattern).
```

**Async on the dedicated compute queue.** When the device exposes a
compute-only family, `device.GetComputeQueue()` is a different `VkQueue`.
Submit your compute work there with its own pool + fence (or a timeline
value) and let the graphics queue keep rendering — rule 18, no shared lock.

---

## 13. GPU particle system (compute sim + indirect draw)

**Intent.** N particles live in a storage buffer. A compute pass advances
them every frame; a graphics pass draws them as instanced point sprites
using `vkCmdDrawIndirect`. Pure GPU loop, CPU only kicks the dispatch.

**Rules.** R1 (explicit barriers between sim and draw), R20 (a particle
example would slot in here at `[14] ParticleExample` if shipped).

**Shaders** — `sim.comp`:
```glsl
#version 450
layout(local_size_x = 64) in;
struct P { vec3 pos; float life; vec3 vel; float pad; };
layout(set=0, binding=0, std430) buffer Particles { P p[]; } P;
layout(push_constant) uniform PC { float dt; uint count; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    P.p[i].pos  += P.p[i].vel * pc.dt;
    P.p[i].vel  += vec3(0, -9.81, 0) * pc.dt;
    P.p[i].life -= pc.dt;
}
```

`particle.vert`:
```glsl
#version 450
layout(set=0, binding=0, std430) readonly buffer Particles {
    struct P { vec3 pos; float life; vec3 vel; float pad; } p[];
} P;
layout(push_constant) uniform PC { mat4 vp; } pc;
void main() {
    gl_Position  = pc.vp * vec4(P.p[gl_InstanceIndex].pos, 1.0);
    gl_PointSize = 4.0;
}
```

**C++** (skeleton):
```cpp
// One-time:
VulkanBuffer particles;        // SSBO, DEVICE_LOCAL, count * sizeof(P)
VulkanBuffer drawIndirect;     // VkDrawIndirectCommand{count,1,0,0}, DEVICE_LOCAL

// Per frame:
// 1) Sim
vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, simPipe.Get());
vkCmdBindDescriptorSets(cb, ..., simSet);
vkCmdPushConstants(cb, simPipe.Layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(simPC), &simPC);
vkCmdDispatch(cb, (count + 63) / 64, 1, 1);

// 2) Barrier: SSBO write -> SSBO read
VkBufferMemoryBarrier b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
b.buffer        = particles.Get();
b.size          = VK_WHOLE_SIZE;
vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0,
                     0, nullptr, 1, &b, 0, nullptr);

// 3) Draw — indirect, GPU decides count
vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipe.Get());
vkCmdBindDescriptorSets(cb, ..., particleSet);
vkCmdDrawIndirect(cb, drawIndirect.Get(), 0, 1, sizeof(VkDrawIndirectCommand));
vkCmdEndRenderPass(cb);
```

Spawn / despawn live in a second compute pass that decrements `count` in
`drawIndirect` atomically. CPU never touches the buffer post-init.

---

## 14. Indirect draw (`vkCmdDrawIndexedIndirect`)

**Intent.** Build a draw list on the GPU (or once on the CPU) and submit it
in one call. Reduces driver overhead vs N `vkCmdDrawIndexed` calls and
unlocks GPU-driven culling.

**Rules.** R1 (you author the indirect buffer), R9 (raw `VkBuffer` ok in
the indirect-buffer slot).

**C++**:
```cpp
struct DrawCmd {                           // matches VkDrawIndexedIndirectCommand
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

// Build CPU-side, upload once:
std::vector<DrawCmd> cmds;
for (auto& mesh : scene) {
    cmds.push_back({mesh.indexCount, 1, mesh.firstIndex, mesh.vertexOffset, 0});
}
VulkanBuffer indirect;
indirect.Initialize(device, cmds.size() * sizeof(DrawCmd),
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
// Upload via VMM staging — see VMM doc / recipe 1 pattern.

// Per frame:
vkCmdBindIndexBuffer(cb, sceneIBO.Get(), 0, VK_INDEX_TYPE_UINT32);
VkBuffer vbos[] = { sceneVBO.Get() };
VkDeviceSize off[] = { 0 };
vkCmdBindVertexBuffers(cb, 0, 1, vbos, off);
vkCmdDrawIndexedIndirect(cb, indirect.Get(), 0,
                         uint32_t(cmds.size()),
                         sizeof(DrawCmd));
```

**GPU-driven culling.** Replace the CPU build with a compute pass that
reads bounding spheres, tests against frustum planes (push constant), and
appends surviving `DrawCmd`s to a second buffer. Use `vkCmdDrawIndexedIndirectCount`
(Vulkan 1.2 / `VK_KHR_draw_indirect_count`) to read the count from a buffer.

---

## 15. Async compute pattern (graphics + compute overlap)

**Intent.** Run a long compute pass (BVH refit, particle sim, GPU culling)
on the dedicated compute queue while the graphics queue keeps rendering
the previous frame's output. Signals a timeline value the graphics queue
waits on next frame.

**Rules.** R4 (no hidden sync — you signal/wait timeline values explicitly),
R18 (different `VkQueue` = different external-sync scope; safe to record
on different threads with different pools), R19 (only kicks in when the
device exposes a dedicated compute family).

**C++**:
```cpp
// One-time: a TimelineSemaphore separate from the scheduler's:
TimelineSemaphore computeTimeline;
computeTimeline.Initialize(device);
uint64_t computeValue = 0;

// Per frame, on the compute thread:
VkCommandBuffer cc = computeCmd.Begin();
RecordHeavyCompute(cc);                    // dispatch(es) + barriers
computeCmd.End();

VkSubmitInfo s{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
s.commandBufferCount = 1;
s.pCommandBuffers    = &cc;

VkTimelineSemaphoreSubmitInfo ts{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
uint64_t signal = ++computeValue;
ts.signalSemaphoreValueCount = 1;
ts.pSignalSemaphoreValues    = &signal;
s.pNext = &ts;

VkSemaphore sig = computeTimeline.Get();
s.signalSemaphoreCount = 1;
s.pSignalSemaphores    = &sig;

vkQueueSubmit(device.GetComputeQueue(), 1, &s, VK_NULL_HANDLE);

// On the graphics submit: wait for that timeline value before reading
// whatever the compute pass wrote (push it as an extra wait semaphore in
// your graphics submit, or chain via DependencyToken if your code uses
// the v0.3 FrameScheduler's timeline).
```

When the device only exposes one queue family (Intel iGPUs often), the
fallback is graphics-queue serial — VCK logs `[Device] Compute queue:
aliased to graphics` once at init, and your code keeps working.

---

## 16. Shadow mapping (directional light + PCF)

**Intent.** One off-screen depth-only pass from the light's POV writes a
shadow map. The main pass projects fragment world-pos into light space,
samples the shadow map with PCF, and modulates direct lighting.

**Rules.** R1 (two render passes, both yours), R22 (shadow map is just a
`VulkanImage` you own).

**Shaders** — `shadow.vert` (depth-only, no fragment):
```glsl
#version 450
layout(location = 0) in vec3 inPos;
layout(push_constant) uniform PC { mat4 lightVP; mat4 model; } pc;
void main() { gl_Position = pc.lightVP * pc.model * vec4(inPos, 1.0); }
```

`lit.frag` (PCF sample of `uShadow`):
```glsl
#version 450
layout(set=0, binding=0) uniform sampler2DShadow uShadow;
layout(location = 0) in  vec4 vLightPos;   // post-perspective, [-1,1]
layout(location = 1) in  vec3 vNormal;
layout(location = 0) out vec4 outColor;

float SamplePCF(vec3 p) {
    vec2 texel = 1.0 / vec2(textureSize(uShadow, 0));
    float sum  = 0.0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
        sum += texture(uShadow, vec3(p.xy + vec2(x,y) * texel, p.z));
    return sum / 9.0;
}

void main() {
    vec3 p = vLightPos.xyz / vLightPos.w;
    p.xy = p.xy * 0.5 + 0.5;                    // NDC -> UV
    if (p.z > 1.0) { outColor = vec4(1); return; }   // out of frustum: lit
    float vis = SamplePCF(p);
    float ndl = max(dot(normalize(vNormal), normalize(vec3(1,1,1))), 0.0);
    outColor  = vec4(vec3(0.1 + 0.9 * vis * ndl), 1.0);
}
```

**C++** (skeleton):
```cpp
// One-time: 2048x2048 D32 SFLOAT, sampled, no color attachment.
VulkanImage shadowMap;
shadowMap.Initialize(device, 2048, 2048, VK_FORMAT_D32_SFLOAT,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
// Build a render pass with one depth attachment, one subpass.
// Build a shadow pipeline using shadow.vert + (no frag) + a depth-only blend state.

// Per frame:
mat4 lightVP = Ortho(-10,10,-10,10, 0.1f, 100.0f) * LookAt(lightPos, vec3(0), vec3(0,1,0));
vkCmdBeginRenderPass(cb, &shadowRP, VK_SUBPASS_CONTENTS_INLINE);
vkCmdBindPipeline(cb, ..., shadowPipe.Get());
vkCmdPushConstants(cb, shadowPipe.Layout(), VK_SHADER_STAGE_VERTEX_BIT,
                   0, sizeof(mat4), &lightVP);
DrawScene(cb);                                    // share VBO/IBO with main pass
vkCmdEndRenderPass(cb);

// Transition shadowMap: DEPTH_STENCIL_ATTACHMENT_OPTIMAL -> DEPTH_READ_ONLY_OPTIMAL.
// Bind in main pass: pass `lightVP` to vert (writes vLightPos), shadowMap in frag.
```

**Tip.** A depth-bias of `+0.005` slope-scaled fixes Peter Pan acne. Use
`VkPipelineRasterizationStateCreateInfo::depthBiasEnable = VK_TRUE`.

---

## 17. Skybox / cubemap rendering

**Intent.** Sample a cubemap with the view direction, draw it behind
everything else with depth-test = LEQUAL and depth-write off.

**Rules.** R9 (raw `VkImageView` cubemap if you bring your own loader),
R22 (you own the cubemap image).

**Shaders** — `sky.vert`:
```glsl
#version 450
layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 vDir;
layout(push_constant) uniform PC { mat4 vp; } pc;
void main() {
    vDir = inPos;                                 // unit cube positions = directions
    vec4 p = pc.vp * vec4(inPos, 1.0);
    gl_Position = p.xyww;                          // force z = w -> always at far plane
}
```

`sky.frag`:
```glsl
#version 450
layout(set=0, binding=0) uniform samplerCube uSky;
layout(location = 0) in  vec3 vDir;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(uSky, normalize(vDir)); }
```

**C++**:
- Build a unit cube VBO (recipe 3 / `Primitives::Cube`).
- Pipeline state: `depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL`,
  `depthWriteEnable = VK_FALSE`, `cullMode = VK_CULL_MODE_FRONT_BIT`
  (we're inside the cube).
- Pass `view-without-translation` in the VP matrix:
  `mat4 vt = mat4(mat3(view)); pc.vp = proj * vt;`.
- Draw the skybox **first** so the depth buffer fills with everything else
  on top. (Drawing last with LEQUAL also works.)

**Loading the cubemap.** Six 2D images stacked into a `VK_IMAGE_VIEW_TYPE_CUBE`
view with `arrayLayers = 6`. Order: +X, -X, +Y, -Y, +Z, -Z. For `.hdr`
equirectangular sources, write a one-time compute pass that samples the
2D HDR and writes 6 face slices.

---

## 18. PBR Cook-Torrance + IBL skeleton

**Intent.** Direct lighting via Cook-Torrance BRDF (GGX + Smith + Fresnel),
plus image-based ambient via pre-filtered specular cubemap + irradiance
cubemap + BRDF LUT (split-sum approximation).

**Rules.** R16 (VCK doesn't ship a material system; this is your shader).

**Shader** — `pbr.frag` (excerpt):
```glsl
#version 450
layout(set=0, binding=0) uniform samplerCube uIrradiance;
layout(set=0, binding=1) uniform samplerCube uPrefilter;
layout(set=0, binding=2) uniform sampler2D    uBrdfLUT;

layout(location=0) in vec3 vWPos;
layout(location=1) in vec3 vN;
layout(location=2) in vec2 vUV;
layout(location=0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec4 cam;        // xyz = camera world pos
    vec4 albedoRM;   // xyz = albedo, w = packed roughness*256+metallic
    vec4 light;      // xyz = direction, w = intensity
} pc;

const float PI = 3.14159265359;

float D_GGX(float NoH, float a) { float a2 = a*a; float d = (NoH*NoH)*(a2-1.0)+1.0; return a2/(PI*d*d); }
float V_SmithGGX(float NoV, float NoL, float a) {
    float a2 = a*a;
    float gv = NoL * sqrt(NoV*NoV*(1.0-a2) + a2);
    float gl = NoV * sqrt(NoL*NoL*(1.0-a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(float u, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - u, 5.0); }

void main() {
    float roughness = floor(pc.albedoRM.w) / 256.0;
    float metallic  = pc.albedoRM.w - floor(pc.albedoRM.w);
    vec3  albedo    = pc.albedoRM.rgb;

    vec3 N = normalize(vN);
    vec3 V = normalize(pc.cam.xyz - vWPos);
    vec3 L = normalize(-pc.light.xyz);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float D = D_GGX(NoH, roughness * roughness);
    float Vt= V_SmithGGX(NoV, NoL, roughness * roughness);
    vec3  F = F_Schlick(VoH, f0);
    vec3  spec = D * Vt * F;
    vec3  diff = (1.0 - F) * (1.0 - metallic) * albedo / PI;
    vec3  direct = (diff + spec) * pc.light.w * NoL;

    // IBL split-sum:
    vec3 R    = reflect(-V, N);
    vec3 irr  = texture(uIrradiance, N).rgb * albedo * (1.0 - metallic);
    float lod = roughness * 4.0;                       // 5 mips, log2(256)=8 etc.
    vec3 prf  = textureLod(uPrefilter, R, lod).rgb;
    vec2 brdf = texture(uBrdfLUT, vec2(NoV, roughness)).rg;
    vec3 ibl  = irr + prf * (f0 * brdf.x + brdf.y);

    outColor = vec4(direct + ibl, 1.0);
}
```

**Pre-pass tooling (offline once).** Generate the BRDF LUT (Hammersley +
GGX importance sample → 512x512 RG16F), the irradiance cubemap (cosine-weighted
hemisphere convolution of the environment), and the pre-filtered specular
cubemap (5 mips, GGX importance sample at increasing roughness). All three
are compute passes you write once, save next to the asset, and load like
any other image (recipe 1 + cubemap from recipe 17).

---

## 19. Deferred shading skeleton (G-buffer + lighting)

**Intent.** Geometry pass writes albedo / normal / position / material to
multiple render targets; lighting pass is a full-screen quad that samples
all of them and does the shading. Cheap with many lights; pricey on
bandwidth and incompatible with MSAA without resolves.

**Rules.** R1 (two passes, both yours), R22 (you own all G-buffer images).

**G-buffer layout** (typical):
```
RT0  R8G8B8A8_SRGB        albedo.rgb, AO in .a
RT1  R16G16B16A16_SFLOAT  normal.xyz (world), roughness in .w
RT2  R16G16B16A16_SFLOAT  worldPos.xyz, metallic in .w
DST  D32_SFLOAT           depth
```

**Shaders** — `gbuffer.frag`:
```glsl
#version 450
layout(location=0) out vec4 outAlbedo;
layout(location=1) out vec4 outNormal;
layout(location=2) out vec4 outPosMat;
layout(location=0) in vec3 vN;
layout(location=1) in vec3 vWPos;
layout(location=2) in vec2 vUV;
layout(push_constant) uniform PC { vec4 albedoMR; } pc;
void main() {
    outAlbedo = vec4(pc.albedoMR.rgb, 1.0);
    outNormal = vec4(normalize(vN) * 0.5 + 0.5, 0.5 /*roughness*/);
    outPosMat = vec4(vWPos, 0.0 /*metallic*/);
}
```

`light.frag` (full-screen quad):
```glsl
#version 450
layout(set=0, binding=0) uniform sampler2D uAlbedo;
layout(set=0, binding=1) uniform sampler2D uNormal;
layout(set=0, binding=2) uniform sampler2D uPosMat;
layout(location=0) in  vec2 vUV;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform PC { vec4 cam; vec4 light; } pc;
void main() {
    vec3 albedo = texture(uAlbedo, vUV).rgb;
    vec3 N      = texture(uNormal, vUV).rgb * 2.0 - 1.0;
    vec3 P      = texture(uPosMat, vUV).rgb;
    vec3 L      = normalize(-pc.light.xyz);
    float ndl   = max(dot(N, L), 0.0);
    outColor    = vec4(albedo * ndl * pc.light.w, 1.0);
}
```

**C++ pipeline.** Two render passes, two pipelines. The G-buffer pass
sets `colorAttachmentCount = 3`, each with its own `VkAttachmentDescription`
and clear value. Between passes, transition the three RTs from
`COLOR_ATTACHMENT_OPTIMAL` → `SHADER_READ_ONLY_OPTIMAL`. The lighting
pass binds them as `combined image samplers` and draws a full-screen
triangle (vertex shader emits NDC from `gl_VertexIndex`).

---

## 20. HDR tonemapping (Reinhard / ACES)

**Intent.** Render scene into a 16-bit float color target so HDR can
exceed 1.0, then collapse to LDR for display via a tonemapper. ACES is the
modern default; Reinhard is the 5-line baseline.

**Rules.** R4 (no hidden sync; one extra pass), R19 (skip the pass and you
get the original behaviour).

**Shader** — `tone.frag`:
```glsl
#version 450
layout(set=0, binding=0) uniform sampler2D uHDR;
layout(location=0) in  vec2 vUV;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform PC { float exposure; uint mode; } pc;

vec3 Reinhard(vec3 c) { return c / (c + 1.0); }

vec3 ACESFitted(vec3 c) {                // Krzysztof Narkowicz' fit
    const float a = 2.51, b = 0.03, c2 = 2.43, d = 0.59, e = 0.14;
    return clamp((c * (a * c + b)) / (c * (c2 * c + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uHDR, vUV).rgb * pc.exposure;
    vec3 ldr = (pc.mode == 1u) ? ACESFitted(hdr) : Reinhard(hdr);
    outColor = vec4(pow(ldr, vec3(1.0 / 2.2)), 1.0);   // gamma encode
}
```

**C++ pipeline.** One offscreen RT (`R16G16B16A16_SFLOAT`), one full-screen
quad pass that writes the swapchain image. Skip the gamma if the swapchain
is already `_SRGB` (you'd be encoding twice).

**Auto-exposure.** Compute pass over the HDR target → log-luminance histogram
→ EV value used as `exposure` next frame. ~150 lines of compute + a one-tap
EMA for stability.

---

## 21. Bloom (bright-pass + mip blur + composite)

**Intent.** Detect bright pixels, blur them at progressively lower
resolutions, add the blurred result back. The "halo around bright lights"
effect. Cheap when done at half-res with mip chains, expensive otherwise.

**Rules.** R4 (one bright-pass + N downsample/upsample passes; all yours).

**Algorithm**:
```
HDR --(bright-pass: keep px where luma > 1.0)--> bloom mip 0
bloom mip 0 -> downsample bilinear -> bloom mip 1
bloom mip 1 -> downsample            -> bloom mip 2
... (5-7 mips)
upsample mip N + tent-filter blur + add to mip N-1
... walk back to mip 0
HDR + bloom mip 0 * intensity -> output
```

**Shader** — `bright.frag`:
```glsl
#version 450
layout(set=0, binding=0) uniform sampler2D uHDR;
layout(location=0) in  vec2 vUV;
layout(location=0) out vec4 outColor;
void main() {
    vec3 c = texture(uHDR, vUV).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    outColor = vec4(c * smoothstep(0.9, 1.4, l), 1.0);
}
```

**Upsample** is a 13-tap tent filter (the "Call of Duty: Advanced Warfare"
preset, well-documented). Each mip pass is ~30 lines of GLSL. 5-7 mip
levels for a 1080p output.

**Tip.** Build the bloom RT chain via `vkCreateImage` with `mipLevels = 7`;
you get a single allocation + N image views, each at half the previous
mip's size.

---

## 22. Shader hot-reload (watch + rebuild + swap)

**Intent.** Edit a `.vert` / `.frag` file, save, see the change next
frame. CPU watches files, recompiles to SPIR-V via `glslc`, re-creates
the `VulkanPipeline`, atomically swaps. A 50ms hitch beats a 30s rebuild
+ relaunch.

**Rules.** R1 (you own the watcher), R4 (recreate on a frame boundary —
between `EndFrame` and the next `BeginFrame` — to avoid in-flight uses),
R19 (off in release builds = zero cost).

**C++** (skeleton, debug-only):
```cpp
struct ShaderWatch {
    std::filesystem::path        path;
    std::filesystem::file_time_type lastWrite;
};
std::vector<ShaderWatch> watches;
VulkanPipeline           current;
VulkanPipeline           pendingDelete;     // freed once GPU is done with it

void Tick(VulkanDevice& device, FrameScheduler& sch) {
    bool dirty = false;
    for (auto& w : watches) {
        auto t = std::filesystem::last_write_time(w.path);
        if (t != w.lastWrite) { w.lastWrite = t; dirty = true; }
    }
    if (!dirty) return;

    // Compile to SPIR-V (system call to glslc, or shaderc in-process).
    if (std::system("glslc shader.frag -o shader.frag.spv") != 0) {
        VCKLog::Warn("HotReload", "glslc failed, keeping old pipeline");
        return;
    }

    // Drain GPU before recreating — same pattern as scheduler-aware resize.
    sch.DrainInFlight();

    VulkanPipeline next;
    if (!next.Initialize(device, "shader.vert.spv", "shader.frag.spv", /*...*/)) {
        VCKLog::Warn("HotReload", "Pipeline init failed; keeping old");
        return;
    }
    pendingDelete = std::move(current);              // RAII teardown next frame
    current       = std::move(next);
    VCKLog::Info("HotReload", "Pipeline reloaded");
}
```

**Production wrinkle.** A real watcher uses OS-specific APIs
(`ReadDirectoryChangesW` on Windows, `inotify` on Linux) instead of
polling timestamps every frame. The polling version above is fine for
dev builds.

---

## 23. GPU picking (object ID readback)

**Intent.** Click on a pixel, find which scene object it belongs to.
Render an extra `R32_UINT` target during the main pass where each draw
writes its object ID, copy that one pixel to a host-visible buffer,
read on the CPU.

**Rules.** R4 (one fence wait per pick — only on click, not every frame),
R14 (fail loud if the readback copy fails).

**Shader** — append to your main `frag`:
```glsl
layout(location = 1) out uint outID;
layout(push_constant) uniform PC { /*...*/ uint objectId; } pc;
// at the end of main():
outID = pc.objectId;
```

**Pipeline.** Add a second color attachment with `VK_FORMAT_R32_UINT`
(blend disabled, write mask = R only).

**C++** (only on mouse click):
```cpp
uint32_t Pick(VulkanDevice& device, VulkanCommand& cmd,
              VulkanImage& idTarget, int px, int py)
{
    VulkanBuffer host;
    host.Initialize(device, sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VulkanOneTimeCommand one(device, cmd.GetPool());
    idTarget.TransitionLayout(one.Get(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy r{};
    r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    r.imageOffset      = { px, py, 0 };
    r.imageExtent      = { 1, 1, 1 };
    vkCmdCopyImageToBuffer(one.Get(), idTarget.Get(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.Get(), 1, &r);

    idTarget.TransitionLayout(one.Get(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    one.End();                                        // per-fence wait inside

    uint32_t id = 0;
    host.Download(&id, sizeof(id));
    return id;                                        // 0 = miss, else object id
}
```

**Cost.** ~1 ms per pick on a discrete GPU, dominated by the `OneTime`
fence wait. Don't pick every frame; pick on click only.

---

## 24. Frustum culling (CPU plane-test vs AABB)

**Intent.** Skip queueing draws whose AABB is entirely outside the camera
frustum. Halves to quarters the draw count for typical scenes; sub-millisecond
on tens of thousands of objects.

**Rules.** R1 (you author the test; VCK draws what you queue),
R20 (would land as `[N] CullingExample` if shipped).

**Math** — extract 6 planes from `proj * view`:
```cpp
struct Plane { VCK::Vec3 n; float d; };               // n.x*x + n.y*y + n.z*z + d = 0

void ExtractFrustum(const VCK::Mat4& vp, Plane out[6]) {
    // Gribb-Hartmann (row-major, vp.m[r*4+c]):
    auto row = [&](int r){ return VCK::Vec4{vp.m[r*4+0], vp.m[r*4+1], vp.m[r*4+2], vp.m[r*4+3]}; };
    VCK::Vec4 R0 = row(0), R1 = row(1), R2 = row(2), R3 = row(3);
    auto plane = [](VCK::Vec4 p){
        float l = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
        return Plane{ {p.x/l, p.y/l, p.z/l}, p.w/l };
    };
    out[0] = plane({R3.x+R0.x, R3.y+R0.y, R3.z+R0.z, R3.w+R0.w});  // left
    out[1] = plane({R3.x-R0.x, R3.y-R0.y, R3.z-R0.z, R3.w-R0.w});  // right
    out[2] = plane({R3.x+R1.x, R3.y+R1.y, R3.z+R1.z, R3.w+R1.w});  // bottom
    out[3] = plane({R3.x-R1.x, R3.y-R1.y, R3.z-R1.z, R3.w-R1.w});  // top
    out[4] = plane({R3.x+R2.x, R3.y+R2.y, R3.z+R2.z, R3.w+R2.w});  // near
    out[5] = plane({R3.x-R2.x, R3.y-R2.y, R3.z-R2.z, R3.w-R2.w});  // far
}
```

**Test** (positive vertex / negative vertex trick):
```cpp
struct AABB { VCK::Vec3 min, max; };

bool AabbInsideFrustum(const Plane fr[6], const AABB& box) {
    for (int i = 0; i < 6; ++i) {
        const VCK::Vec3& n = fr[i].n;
        VCK::Vec3 p {                                  // positive vertex
            (n.x >= 0 ? box.max.x : box.min.x),
            (n.y >= 0 ? box.max.y : box.min.y),
            (n.z >= 0 ? box.max.z : box.min.z),
        };
        if (n.x*p.x + n.y*p.y + n.z*p.z + fr[i].d < 0) return false;
    }
    return true;
}
```

**Use**:
```cpp
Plane fr[6]; ExtractFrustum(proj * view, fr);
for (auto& obj : scene)
    if (AabbInsideFrustum(fr, obj.aabb))
        Queue(obj);
```

**Tighter culling.** Add a sphere-then-AABB hierarchy (fast reject most
objects with the cheaper sphere test). For static scenes, build a BVH
once and traverse top-down with frustum-vs-node tests; skip whole subtrees
on miss.

**GPU culling** (advanced). Move the plane test into a compute pass that
writes survivors to an indirect buffer (recipe 14). Same math, runs on
thousands of cores instead of one CPU thread.

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
