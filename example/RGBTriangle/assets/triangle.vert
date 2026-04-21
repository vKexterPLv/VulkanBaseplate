#version 450

// =============================================================================
//  triangle.vert — RGBTriangle
//
//  No descriptors, no push constants, no UBO.
//  VulkanPipeline is used directly with an empty pipeline layout.
//
//  in  loc 0  vec3  inPosition  — vertex position (NDC)
//  in  loc 1  vec4  inColor     — per-vertex RGBA color
//  out loc 0  vec4  outColor    — passed to fragment stage
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor    = inColor;
    gl_Position = vec4(inPosition, 1.0);
}
