#version 450

// =============================================================================
//  shadertool.vert  -  ShaderToolingExample
//
//  Matches the VertexLayout declared via VCK::ShaderStage in App.cpp:
//    .Vertex().Add("position", Vec3).Add("color", Vec3)
//
//  Matches the PushConstants declared via VCK::ShaderStage::Push():
//    .Declare("mvp", Mat4)
//
//  No specialization constants on the vertex side - keeps the spec wiring
//  story for the fragment shader where it's easier to see.
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

layout(location = 0) out vec3 vColor;

void main()
{
    vColor      = inColor;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
