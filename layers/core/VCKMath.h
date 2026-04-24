// =============================================================================
//  layers/core/VCKMath.h
//
//  Minimal POD math types for shader inputs.  Vec2/Vec3/Vec4 + Mat4 are
//  layout-compatible with GLSL `vec2` / `vec3` / `vec4` / `mat4` (std140
//  rules notwithstanding - see below), so a `memcpy` into a push-constant
//  or uniform buffer "just works".
//
//  Design rules in effect:
//    R1  - Every value is a POD.  No hidden constructors, no templates,
//          no SIMD.  What you write is what hits the GPU.
//    R15 - Minimum viable maths.  We ship what a shader input needs:
//          Vec2/3/4, Mat4, plus translate/rotate/scale/perspective/lookAt.
//          No quaternions, no SIMD, no full matrix algebra.
//    R16 - Not an engine maths library.  If you need GLM, ship GLM; VCK
//          does not compete with it.
//    R22 - Values are returned by value.  Callers own them.
//
//  Layout notes
//    - Vec3 is 12 bytes, NOT 16.  If you put a Vec3 in a std140 uniform
//      block, GLSL requires 16-byte alignment; pad manually or promote
//      to Vec4.  For push constants and plain vertex attributes there is
//      no alignment surprise.
//    - Mat4 is column-major - the same convention Vulkan GLSL uses.
//      `Mat4::m[16]` is laid out as [col0.x col0.y col0.z col0.w
//       col1.x col1.y ...].  `operator[]` returns a column as Vec4.
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace VCK
{
    // -------------------------------------------------------------------------
    // Vec2 / Vec3 / Vec4
    // -------------------------------------------------------------------------
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;

        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        explicit Vec3(float s) : x(s), y(s), z(s) {}
    };

    struct Vec4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        Vec4() = default;
        Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
        Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    };

    // -------------------------------------------------------------------------
    // Vec2 arithmetic
    // -------------------------------------------------------------------------
    inline Vec2 operator+(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
    inline Vec2 operator-(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
    inline Vec2 operator*(const Vec2& a, float s)       { return { a.x * s,   a.y * s   }; }
    inline Vec2 operator*(float s,        const Vec2& a){ return a * s; }
    inline float Dot   (const Vec2& a, const Vec2& b)   { return a.x * b.x + a.y * b.y; }
    inline float Length(const Vec2& a)                  { return std::sqrt(Dot(a, a)); }

    // -------------------------------------------------------------------------
    // Vec3 arithmetic
    // -------------------------------------------------------------------------
    inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3 operator-(const Vec3& a)                { return { -a.x, -a.y, -a.z }; }
    inline Vec3 operator*(const Vec3& a, float s)       { return { a.x * s,   a.y * s,   a.z * s   }; }
    inline Vec3 operator*(float s,        const Vec3& a){ return a * s; }
    inline Vec3 operator/(const Vec3& a, float s)       { return { a.x / s,   a.y / s,   a.z / s   }; }
    inline float Dot   (const Vec3& a, const Vec3& b)   { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3  Cross (const Vec3& a, const Vec3& b)
    {
        return { a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x };
    }
    inline float Length   (const Vec3& a) { return std::sqrt(Dot(a, a)); }
    inline Vec3  Normalize(const Vec3& a)
    {
        const float len = Length(a);
        return len > 0.0f ? a / len : Vec3{};
    }

    // -------------------------------------------------------------------------
    // Vec4 arithmetic (just enough for Mat4 * Vec4)
    // -------------------------------------------------------------------------
    inline Vec4 operator+(const Vec4& a, const Vec4& b) { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
    inline Vec4 operator*(const Vec4& a, float s)       { return { a.x * s,   a.y * s,   a.z * s,   a.w * s   }; }
    inline float Dot(const Vec4& a, const Vec4& b)      { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }


    // -------------------------------------------------------------------------
    // Mat4 (column-major, identity by default)
    //
    //    m[0..3]   = column 0
    //    m[4..7]   = column 1
    //    m[8..11]  = column 2
    //    m[12..15] = column 3
    //
    // Access the element at (row r, column c) via m[c*4 + r].  The helper
    // At(r, c) does exactly that; use it instead of raw indexing where it
    // improves readability.
    // -------------------------------------------------------------------------
    struct Mat4
    {
        float m[16] = { 1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f };

        Mat4() = default;
        // Explicit 16-float constructor; column-major order.
        Mat4(float m00, float m10, float m20, float m30,
             float m01, float m11, float m21, float m31,
             float m02, float m12, float m22, float m32,
             float m03, float m13, float m23, float m33)
            : m{ m00, m10, m20, m30,
                 m01, m11, m21, m31,
                 m02, m12, m22, m32,
                 m03, m13, m23, m33 } {}

        float&       At(int row, int col)       { return m[col * 4 + row]; }
        const float& At(int row, int col) const { return m[col * 4 + row]; }
    };

    // -------------------------------------------------------------------------
    // Mat4 arithmetic
    // -------------------------------------------------------------------------
    inline Mat4 operator*(const Mat4& a, const Mat4& b)
    {
        Mat4 r{};
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k)
                    s += a.At(row, k) * b.At(k, col);
                r.At(row, col) = s;
            }
        }
        return r;
    }

    inline Vec4 operator*(const Mat4& a, const Vec4& v)
    {
        return {
            a.At(0, 0) * v.x + a.At(0, 1) * v.y + a.At(0, 2) * v.z + a.At(0, 3) * v.w,
            a.At(1, 0) * v.x + a.At(1, 1) * v.y + a.At(1, 2) * v.z + a.At(1, 3) * v.w,
            a.At(2, 0) * v.x + a.At(2, 1) * v.y + a.At(2, 2) * v.z + a.At(2, 3) * v.w,
            a.At(3, 0) * v.x + a.At(3, 1) * v.y + a.At(3, 2) * v.z + a.At(3, 3) * v.w,
        };
    }

    inline Mat4 Transpose(const Mat4& a)
    {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr)
                r.At(rr, c) = a.At(c, rr);
        return r;
    }

    // -------------------------------------------------------------------------
    // Affine builders
    // -------------------------------------------------------------------------
    inline Mat4 Translate(const Vec3& t)
    {
        Mat4 r{};
        r.At(0, 3) = t.x;
        r.At(1, 3) = t.y;
        r.At(2, 3) = t.z;
        return r;
    }

    inline Mat4 Scale(const Vec3& s)
    {
        Mat4 r{};
        r.At(0, 0) = s.x;
        r.At(1, 1) = s.y;
        r.At(2, 2) = s.z;
        return r;
    }

    // Rodrigues rotation around a (unit) axis by `radians`.
    // Axis is normalised internally so callers may pass non-unit vectors.
    inline Mat4 Rotate(const Vec3& axis, float radians)
    {
        const Vec3  a = Normalize(axis);
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        const float oc = 1.0f - c;

        Mat4 r{};
        r.At(0, 0) = c + a.x * a.x * oc;
        r.At(0, 1) = a.x * a.y * oc - a.z * s;
        r.At(0, 2) = a.x * a.z * oc + a.y * s;

        r.At(1, 0) = a.y * a.x * oc + a.z * s;
        r.At(1, 1) = c + a.y * a.y * oc;
        r.At(1, 2) = a.y * a.z * oc - a.x * s;

        r.At(2, 0) = a.z * a.x * oc - a.y * s;
        r.At(2, 1) = a.z * a.y * oc + a.x * s;
        r.At(2, 2) = c + a.z * a.z * oc;

        return r;
    }

    // -------------------------------------------------------------------------
    // Camera / projection
    //
    //  Perspective is Vulkan-ready:
    //    - right-handed, looking down -Z
    //    - depth range [0, 1]
    //    - Y-axis flipped (Vulkan NDC Y points down)
    //  Pass the returned matrix straight to your shader's `projection`
    //  uniform; no extra flip needed.
    // -------------------------------------------------------------------------
    inline Mat4 Perspective(float fovYRadians, float aspect, float zNear, float zFar)
    {
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r{};
        // Zero-initialised identity default is fine; we overwrite everything.
        for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
        r.At(0, 0) =  f / aspect;
        r.At(1, 1) = -f;                                // Vulkan Y-flip
        r.At(2, 2) =  zFar / (zNear - zFar);            // depth [0,1]
        r.At(2, 3) =  (zFar * zNear) / (zNear - zFar);
        r.At(3, 2) = -1.0f;
        return r;
    }

    inline Mat4 LookAt(const Vec3& eye, const Vec3& at, const Vec3& up)
    {
        const Vec3 f = Normalize(at - eye);
        const Vec3 s = Normalize(Cross(f, up));
        const Vec3 u = Cross(s, f);

        Mat4 r{};
        r.At(0, 0) =  s.x; r.At(0, 1) =  s.y; r.At(0, 2) =  s.z; r.At(0, 3) = -Dot(s, eye);
        r.At(1, 0) =  u.x; r.At(1, 1) =  u.y; r.At(1, 2) =  u.z; r.At(1, 3) = -Dot(u, eye);
        r.At(2, 0) = -f.x; r.At(2, 1) = -f.y; r.At(2, 2) = -f.z; r.At(2, 3) =  Dot(f, eye);
        r.At(3, 0) =  0.0f; r.At(3, 1) = 0.0f; r.At(3, 2) = 0.0f; r.At(3, 3) = 1.0f;
        return r;
    }

    // -------------------------------------------------------------------------
    // Convenience - degrees wrapper (shader code sometimes wants degrees).
    // -------------------------------------------------------------------------
    inline float Radians(float degrees) { return degrees * 0.017453292519943295f; }
    inline float Degrees(float radians) { return radians * 57.29577951308232f;   }

    // Sanity checks so byte-blits to push constants / UBOs never silently
    // break.  Covered by R1.
    static_assert(sizeof(Vec2) == 2 * sizeof(float), "VCK::Vec2 must be tightly packed");
    static_assert(sizeof(Vec3) == 3 * sizeof(float), "VCK::Vec3 must be tightly packed");
    static_assert(sizeof(Vec4) == 4 * sizeof(float), "VCK::Vec4 must be tightly packed");
    static_assert(sizeof(Mat4) == 16 * sizeof(float), "VCK::Mat4 must be tightly packed");
} // namespace VCK
