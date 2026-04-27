// =============================================================================
//  test_vckmath.cpp
//
//  VCKMath — Vec2 / Vec3 / Vec4 / Mat4 correctness
//
//  Rules touched: R19 (zero cost, no GPU needed), R16 (no engine assumptions —
//  VCKMath is just math, not a scene graph), R15 (minimal surface).
//
//  Every function here runs without a Vulkan device — that's the point.
//  If any math function silently produces wrong results, shaders get wrong
//  transforms and nothing renders correctly. These are the silent failures
//  R14 can't catch at runtime because the GPU just draws garbage.
//  Tests here catch them at build time.
// =============================================================================

#include "vck_test.h"
#include "VCK.h"
#include <cmath>

static bool Near(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) < eps;
}

// ---------------------------------------------------------------------------
//  Vec3
// ---------------------------------------------------------------------------

TEST(VCKMath_Vec3, add)
{
    VCK::Vec3 a{1, 2, 3}, b{4, 5, 6};
    VCK::Vec3 r = a + b;
    ASSERT_TRUE(Near(r.x, 5)); ASSERT_TRUE(Near(r.y, 7)); ASSERT_TRUE(Near(r.z, 9));
}

TEST(VCKMath_Vec3, subtract)
{
    VCK::Vec3 a{5, 5, 5}, b{1, 2, 3};
    VCK::Vec3 r = a - b;
    ASSERT_TRUE(Near(r.x, 4)); ASSERT_TRUE(Near(r.y, 3)); ASSERT_TRUE(Near(r.z, 2));
}

TEST(VCKMath_Vec3, scale)
{
    VCK::Vec3 v{1, 2, 3};
    VCK::Vec3 r = v * 2.f;
    ASSERT_TRUE(Near(r.x, 2)); ASSERT_TRUE(Near(r.y, 4)); ASSERT_TRUE(Near(r.z, 6));
}

TEST(VCKMath_Vec3, dot_product)
{
    VCK::Vec3 a{1, 0, 0}, b{0, 1, 0};
    ASSERT_TRUE(Near(VCK::Dot(a, b), 0.f)); // perpendicular
    ASSERT_TRUE(Near(VCK::Dot(a, a), 1.f)); // self = length^2
}

TEST(VCKMath_Vec3, cross_product)
{
    VCK::Vec3 x{1, 0, 0}, y{0, 1, 0};
    VCK::Vec3 z = VCK::Cross(x, y);
    ASSERT_TRUE(Near(z.x, 0)); ASSERT_TRUE(Near(z.y, 0)); ASSERT_TRUE(Near(z.z, 1));
}

TEST(VCKMath_Vec3, length)
{
    VCK::Vec3 v{3, 4, 0};
    ASSERT_TRUE(Near(VCK::Length(v), 5.f)); // 3-4-5 right triangle
}

TEST(VCKMath_Vec3, normalize_unit_length)
{
    VCK::Vec3 v{3, 0, 0};
    VCK::Vec3 n = VCK::Normalize(v);
    ASSERT_TRUE(Near(VCK::Length(n), 1.f));
    ASSERT_TRUE(Near(n.x, 1.f));
}

TEST(VCKMath_Vec3, negate)
{
    VCK::Vec3 v{1, -2, 3};
    VCK::Vec3 r = -v;
    ASSERT_TRUE(Near(r.x, -1)); ASSERT_TRUE(Near(r.y, 2)); ASSERT_TRUE(Near(r.z, -3));
}

// ---------------------------------------------------------------------------
//  Vec2
// ---------------------------------------------------------------------------

TEST(VCKMath_Vec2, add)
{
    VCK::Vec2 a{1, 2}, b{3, 4};
    VCK::Vec2 r = a + b;
    ASSERT_TRUE(Near(r.x, 4)); ASSERT_TRUE(Near(r.y, 6));
}

TEST(VCKMath_Vec2, dot)
{
    VCK::Vec2 a{1, 0}, b{0, 1};
    ASSERT_TRUE(Near(VCK::Dot(a, b), 0.f));
}

TEST(VCKMath_Vec2, length)
{
    VCK::Vec2 v{3, 4};
    ASSERT_TRUE(Near(VCK::Length(v), 5.f));
}

// ---------------------------------------------------------------------------
//  Mat4
// ---------------------------------------------------------------------------

TEST(VCKMath_Mat4, identity_times_vec4_unchanged)
{
    // Identity * v = v — fundamental matrix property
    VCK::Mat4 I{}; // default is identity
    VCK::Vec4 v{1, 2, 3, 1};
    VCK::Vec4 r = I * v;
    ASSERT_TRUE(Near(r.x, 1)); ASSERT_TRUE(Near(r.y, 2));
    ASSERT_TRUE(Near(r.z, 3)); ASSERT_TRUE(Near(r.w, 1));
}

TEST(VCKMath_Mat4, identity_times_identity_is_identity)
{
    VCK::Mat4 I{};
    VCK::Mat4 R = I * I;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            ASSERT_TRUE(Near(R.m[col][row], (col == row) ? 1.f : 0.f));
}

TEST(VCKMath_Mat4, translate_moves_point)
{
    VCK::Mat4 T = VCK::Translate({3, 4, 5});
    VCK::Vec4 p{0, 0, 0, 1};
    VCK::Vec4 r = T * p;
    ASSERT_TRUE(Near(r.x, 3)); ASSERT_TRUE(Near(r.y, 4)); ASSERT_TRUE(Near(r.z, 5));
}

TEST(VCKMath_Mat4, translate_does_not_move_direction)
{
    // Direction vectors (w=0) must not be translated
    VCK::Mat4 T = VCK::Translate({3, 4, 5});
    VCK::Vec4 d{1, 0, 0, 0}; // direction, not point
    VCK::Vec4 r = T * d;
    ASSERT_TRUE(Near(r.x, 1)); ASSERT_TRUE(Near(r.y, 0)); ASSERT_TRUE(Near(r.z, 0));
}

TEST(VCKMath_Mat4, scale_scales_point)
{
    VCK::Mat4 S = VCK::Scale({2, 3, 4});
    VCK::Vec4 p{1, 1, 1, 1};
    VCK::Vec4 r = S * p;
    ASSERT_TRUE(Near(r.x, 2)); ASSERT_TRUE(Near(r.y, 3)); ASSERT_TRUE(Near(r.z, 4));
}

TEST(VCKMath_Mat4, transpose_of_identity_is_identity)
{
    VCK::Mat4 I{};
    VCK::Mat4 T = VCK::Transpose(I);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            ASSERT_TRUE(Near(T.m[col][row], (col == row) ? 1.f : 0.f));
}

TEST(VCKMath_Mat4, rotate_90_around_y_maps_x_to_neg_z)
{
    // 90° rotation around Y: X axis → -Z axis
    VCK::Mat4 R = VCK::Rotate({0, 1, 0}, VCK::Radians(90.f));
    VCK::Vec4 x{1, 0, 0, 0};
    VCK::Vec4 r = R * x;
    ASSERT_TRUE(Near(r.x,  0.f, 1e-4f));
    ASSERT_TRUE(Near(r.y,  0.f, 1e-4f));
    ASSERT_TRUE(Near(r.z, -1.f, 1e-4f));
}

TEST(VCKMath_Mat4, perspective_w_component_from_z)
{
    // Perspective must write -z into w so the perspective divide works
    float fov    = VCK::Radians(60.f);
    float aspect = 16.f / 9.f;
    VCK::Mat4 P = VCK::Perspective(fov, aspect, 0.1f, 100.f);
    VCK::Vec4 p{0, 0, -5.f, 1}; // point 5 units in front
    VCK::Vec4 r = P * p;
    // w should be 5 (= -(-5)) for correct perspective divide
    ASSERT_TRUE(Near(r.w, 5.f, 1e-3f));
}

TEST(VCKMath_Mat4, lookat_z_axis_points_to_target)
{
    // LookAt: camera at origin looking at (0,0,-1). Forward in view space = +Z.
    VCK::Mat4 V = VCK::LookAt({0,0,0}, {0,0,-1}, {0,1,0});
    VCK::Vec4 target{0, 0, -1, 1};
    VCK::Vec4 r = V * target;
    // In view space the target should be at positive Z (in front)
    ASSERT_TRUE(r.z > 0.f);
}

// ---------------------------------------------------------------------------
//  Radians / Degrees
// ---------------------------------------------------------------------------

TEST(VCKMath_Util, radians_of_180_is_pi)
{
    ASSERT_TRUE(Near(VCK::Radians(180.f), 3.14159265f, 1e-5f));
}

TEST(VCKMath_Util, degrees_of_pi_is_180)
{
    ASSERT_TRUE(Near(VCK::Degrees(3.14159265f), 180.f, 1e-3f));
}

TEST(VCKMath_Util, radians_degrees_roundtrip)
{
    float orig = 45.f;
    ASSERT_TRUE(Near(VCK::Degrees(VCK::Radians(orig)), orig, 1e-4f));
}
