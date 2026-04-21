#version 450

// =============================================================================
//  vmm.vert — VMMExample
//
//  Used by both the animated quad (transient) and the static triangle
//  (persistent).  The same pipeline draws both — the triangle ignores uv,
//  the quad ignores color, the shader handles both correctly.
//
//  Vertex inputs
//  ─────────────
//  loc 0  vec3  inPosition
//  loc 1  vec2  inUV
//  loc 2  vec4  inColor
//
//  Descriptors
//  ───────────
//  set 0, binding 0  FrameUBO { mat4 transform }  — identity, unused
//
//  Push constant
//  ─────────────
//  offset 0, 64 bytes — mat4 model (identity)
//
//  Outputs
//  ───────
//  loc 0  vec2  outUV
//  loc 1  vec4  outColor
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 transform;
} ubo;

layout(push_constant) uniform PC {
    mat4 model;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main()
{
    outUV       = inUV;
    outColor    = inColor;
    gl_Position = pc.model * vec4(inPosition, 1.0);
}
