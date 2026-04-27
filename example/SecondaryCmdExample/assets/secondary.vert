#version 450

// =============================================================================
//  secondary.vert — SecondaryCmdExample
//
//  Identical to RGBTriangle's vertex shader; copied here so the example
//  is self-contained.
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor    = inColor;
    gl_Position = vec4(inPosition, 1.0);
}
