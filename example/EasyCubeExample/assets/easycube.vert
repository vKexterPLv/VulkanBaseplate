#version 450

// =============================================================================
//  easycube.vert  -  EasyCubeExample
//
//  Matches VCK::VertexLayout declared in App.cpp:
//    .Add("position", Vec3)
//    .Add("normal",   Vec3)
//
//  Matches VCK::PushConstants declared in App.cpp:
//    .Declare("mvp", Mat4)
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

layout(location = 0) out vec3 vNormal;

void main()
{
    vNormal     = inNormal;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
