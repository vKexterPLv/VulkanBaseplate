#version 450

// =============================================================================
//  vmm.frag — VMMExample
//
//  The quad samples the checkerboard texture and multiplies by vertex color.
//  The triangle has UV (0,0) everywhere and white vertex color, so it reads
//  the top-left texel of the checkerboard — that's fine for this demo since
//  the triangle's color comes entirely from inColor anyway.
//
//  Descriptors
//  ───────────
//  set 1, binding 0  sampler2D texSampler  — checkerboard texture
//
//  Inputs
//  ──────
//  loc 0  vec2  inUV
//  loc 1  vec4  inColor
//
//  Output
//  ──────
//  loc 0  vec4  outColor
// =============================================================================

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in  vec2 inUV;
layout(location = 1) in  vec4 inColor;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(texSampler, inUV);

    // Quad: inColor is white (1,1,1,1) → pure texture color
    // Triangle: inColor is the vertex color → tints over the texel
    outColor = texColor * inColor;
}
