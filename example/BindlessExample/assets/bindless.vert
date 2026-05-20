#version 450

// =============================================================================
//  bindless.vert — BindlessExample
//
//  Passthrough vertex shader: forwards position and UV to the fragment stage.
//
//  in  loc 0  vec2  inPos   — vertex position (NDC)
//  in  loc 1  vec2  inUV    — texture coordinate
//  out loc 0  vec2  outUV   — passed to fragment stage
// =============================================================================

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;

void main()
{
    outUV       = inUV;
    gl_Position = vec4(inPos, 0.0, 1.0);
}
