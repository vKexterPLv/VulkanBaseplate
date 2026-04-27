#version 450

// =============================================================================
//  hello.vert — HelloExample
//
//  stb_easy_font produces quads in *pixel* coordinates; App.cpp converts them
//  to NDC before upload, so this shader is just a straight passthrough.
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor    = inColor;
    gl_Position = vec4(inPosition, 1.0);
}
