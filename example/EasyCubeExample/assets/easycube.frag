#version 450

// =============================================================================
//  easycube.frag  -  EasyCubeExample
//
//  Cheap Lambert-ish shading off the interpolated normal.  The interpolated
//  normal across a flat face stays flat, so each face gets one constant
//  shade - what you want for showing the cube topology clearly.
// =============================================================================

layout(location = 0) in  vec3 vNormal;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3  n      = normalize(vNormal);
    vec3  lDir   = normalize(vec3(0.35, 0.85, 0.25));
    float lambert = max(dot(n, lDir), 0.0);

    // Slight ambient so back faces aren't pure black.
    vec3 base  = vec3(0.78, 0.82, 0.90);
    vec3 color = base * (0.18 + 0.82 * lambert);

    outColor = vec4(color, 1.0);
}
