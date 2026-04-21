#version 450

// =============================================================================
//  mip.vert — MipmapExample vertex shader
//
//  Inputs
//  ──────
//  location 0  vec3  inPosition   — vertex position (NDC-space in this example)
//  location 1  vec2  inUV         — texture coordinates [0, 1]
//
//  Descriptors
//  ───────────
//  set 0, binding 0  — per-frame UBO (mat4); ignored here, model stays identity
//
//  Push constant
//  ─────────────
//  offset 0, 64 bytes — mat4 model; identity in this example
//
//  Outputs
//  ───────
//  gl_Position  — clip-space position
//  outUV        — passed through to fragment stage
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

// set 0, binding 0 — per-frame UBO (mat4 viewProj + camera data)
// Not used in this example — quad is already in NDC.
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 transform;
} ubo;

// Push constant — mat4 model (64 bytes)
// Identity in this example.
layout(push_constant) uniform PC {
    mat4 model;
} pc;

layout(location = 0) out vec2 outUV;

void main()
{
    outUV       = inUV;
    gl_Position = pc.model * vec4(inPosition, 1.0);
}
