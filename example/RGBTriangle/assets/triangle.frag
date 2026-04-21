#version 450

// =============================================================================
//  triangle.frag — RGBTriangle
//
//  Passes the interpolated vertex color straight to the swapchain.
//
//  in  loc 0  vec4  inColor   — interpolated from vertex stage
//  out loc 0  vec4  outColor  — final swapchain pixel
// =============================================================================

layout(location = 0) in  vec4 inColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = inColor;
}
