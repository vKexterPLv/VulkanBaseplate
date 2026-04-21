#version 450

// =============================================================================
//  mip.frag — MipmapExample fragment shader
//
//  Samples the mip-aware checkerboard texture.  Because the sampler was
//  created with LINEAR filter, VK_SAMPLER_MIPMAP_MODE_LINEAR, and
//  maxLod = g_MipLevels, the GPU automatically selects the appropriate mip
//  level based on the screen-space derivative of the UV coordinates.
//
//  At normal window size: level 0 (sharp checkerboard) is visible.
//  Resize the window very small: coarser mip levels kick in and the
//  checkerboard averages toward grey — this is the mip system working.
//
//  Descriptors
//  ───────────
//  set 1, binding 0 — combined image sampler (the mip-chain texture)
//
//  Inputs
//  ──────
//  location 0  vec2  inUV  — interpolated texture coordinates from vertex stage
//
//  Output
//  ──────
//  location 0  vec4  outColor  — final RGBA colour written to the swapchain
// =============================================================================

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(texSampler, inUV);
}
