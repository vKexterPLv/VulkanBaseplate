#version 450
#extension GL_EXT_nonuniform_qualifier : require

// =============================================================================
//  bindless.frag — BindlessExample
//
//  Samples one texture from the bindless array using a push-constant index.
//
//  set 0, binding 0   — bindless sampler2D array (PARTIALLY_BOUND, UPDATE_AFTER_BIND)
//  push constant      — texIdx: which slot in the array to sample
//
//  in  loc 0  vec2  inUV      — texture coordinate from vertex stage
//  out loc 0  vec4  outColor  — final swapchain pixel
// =============================================================================

layout(set = 0, binding = 0) uniform sampler2D textures[4];

layout(push_constant) uniform PC
{
    uint texIdx;
} pc;

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(textures[nonuniformEXT(pc.texIdx)], inUV);
}
