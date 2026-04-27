#version 450

// =============================================================================
//  shadertool.frag  -  ShaderToolingExample
//
//  Specialization constant 0 = brightness multiplier (float, default 1.0).
//
//  The example pins it to 0.85 via VCK::SpecConstants::Set(0, 0.85f) so the
//  triangle renders slightly dimmed compared to the raw vertex colors.
//  Editing the SpecConstants line in App.cpp (or hot-replacing the .spv
//  with a different default) rerolls the brightness without rebuilding
//  the shader.
// =============================================================================

layout(constant_id = 0) const float BRIGHTNESS = 1.0;

layout(location = 0) in  vec3 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vColor * BRIGHTNESS, 1.0);
}
