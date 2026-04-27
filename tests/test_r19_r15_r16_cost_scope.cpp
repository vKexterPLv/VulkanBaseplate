// =============================================================================
//  test_r19_r15_r16_cost_scope.cpp
//
//  Category IV — Cost & Scope
//  Rules: R15, R16, R19
//
//  R15 — Minimal core surface. Complexity belongs in expansion layers.
//  R16 — No engine assumptions. VCK provides primitives only.
//  R19 — Zero cost for unused features. Un-Initialize'd modules cost nothing.
//
//  Tests here verify the zero-cost and scope promises using classes that
//  need no GPU device: VertexLayout, PushConstants, Primitives, VCKMath.
//  These classes must be fully testable without Vulkan initialization —
//  that itself proves they carry no hidden GPU state (R19).
// =============================================================================

#include "vck_test.h"
#include "VCK.h"
#include <cmath>

// Tolerance for float comparisons
static bool NearEq(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) < eps;
}

// ---------------------------------------------------------------------------
//  VertexLayout [23] — R19: testable with zero GPU init
// ---------------------------------------------------------------------------

TEST(VertexLayout_R19, empty_layout_stride_is_zero)
{
    // R19: default-constructed VertexLayout costs nothing — stride is zero,
    // no allocations, no GPU objects.
    VCK::VertexLayout vl;
    ASSERT_EQ(vl.Stride(), 0u);
    ASSERT_EQ(vl.Count(),  static_cast<std::size_t>(0));
}

TEST(VertexLayout_R19, float_is_4_bytes)
{
    VCK::VertexLayout vl;
    vl.Add("scalar", VCK::VertexAttrType::Float);
    ASSERT_EQ(vl.Stride(), 4u);
}

TEST(VertexLayout_R19, vec2_is_8_bytes)
{
    VCK::VertexLayout vl;
    vl.Add("uv", VCK::VertexAttrType::Vec2);
    ASSERT_EQ(vl.Stride(), 8u);
}

TEST(VertexLayout_R19, vec3_is_12_bytes)
{
    VCK::VertexLayout vl;
    vl.Add("pos", VCK::VertexAttrType::Vec3);
    ASSERT_EQ(vl.Stride(), 12u);
}

TEST(VertexLayout_R19, vec4_is_16_bytes)
{
    VCK::VertexLayout vl;
    vl.Add("col", VCK::VertexAttrType::Vec4);
    ASSERT_EQ(vl.Stride(), 16u);
}

TEST(VertexLayout_R19, int_is_4_bytes)
{
    VCK::VertexLayout vl;
    vl.Add("id", VCK::VertexAttrType::Int);
    ASSERT_EQ(vl.Stride(), 4u);
}

TEST(VertexLayout_R19, uint_is_4_bytes)
{
    VCK::VertexLayout vl;
    vl.Add("flags", VCK::VertexAttrType::UInt);
    ASSERT_EQ(vl.Stride(), 4u);
}

TEST(VertexLayout_R19, typical_pos_normal_uv_stride_is_32)
{
    // Real-world layout: position(12) + normal(12) + uv(8) = 32 bytes
    VCK::VertexLayout vl;
    vl.Add("position", VCK::VertexAttrType::Vec3)
      .Add("normal",   VCK::VertexAttrType::Vec3)
      .Add("uv",       VCK::VertexAttrType::Vec2);
    ASSERT_EQ(vl.Stride(), 32u);
    ASSERT_EQ(vl.Count(),  static_cast<std::size_t>(3));
}

TEST(VertexLayout_R19, attribute_count_matches_adds)
{
    VCK::VertexLayout vl;
    vl.Add("a", VCK::VertexAttrType::Vec3)
      .Add("b", VCK::VertexAttrType::Vec3)
      .Add("c", VCK::VertexAttrType::Vec2)
      .Add("d", VCK::VertexAttrType::Float);
    ASSERT_EQ(vl.Count(), static_cast<std::size_t>(4));
}

TEST(VertexLayout_R19, binding_stride_matches_layout_stride)
{
    // Binding(0).stride must equal Stride() — they describe the same data
    VCK::VertexLayout vl;
    vl.Add("pos", VCK::VertexAttrType::Vec3)
      .Add("uv",  VCK::VertexAttrType::Vec2);
    ASSERT_EQ(vl.Binding(0).stride, vl.Stride());
}

TEST(VertexLayout_R19, attributes_count_matches_count)
{
    VCK::VertexLayout vl;
    vl.Add("pos",    VCK::VertexAttrType::Vec3)
      .Add("normal", VCK::VertexAttrType::Vec3)
      .Add("uv",     VCK::VertexAttrType::Vec2);
    ASSERT_EQ(vl.Attributes(0).size(), vl.Count());
}

TEST(VertexLayout_R19, location_indices_are_sequential)
{
    // Locations must be 0, 1, 2 in order — matches GLSL layout(location=N)
    VCK::VertexLayout vl;
    vl.Add("pos",    VCK::VertexAttrType::Vec3)
      .Add("normal", VCK::VertexAttrType::Vec3)
      .Add("uv",     VCK::VertexAttrType::Vec2);
    auto attrs = vl.Attributes(0);
    ASSERT_EQ(attrs[0].location, 0u);
    ASSERT_EQ(attrs[1].location, 1u);
    ASSERT_EQ(attrs[2].location, 2u);
}

TEST(VertexLayout_R19, offsets_pack_tightly)
{
    // Offsets: pos=0, normal=12, uv=24 — no padding between attributes
    VCK::VertexLayout vl;
    vl.Add("pos",    VCK::VertexAttrType::Vec3)
      .Add("normal", VCK::VertexAttrType::Vec3)
      .Add("uv",     VCK::VertexAttrType::Vec2);
    auto attrs = vl.Attributes(0);
    ASSERT_EQ(attrs[0].offset, 0u);
    ASSERT_EQ(attrs[1].offset, 12u);
    ASSERT_EQ(attrs[2].offset, 24u);
}

TEST(VertexLayout_R19, fluent_chain_returns_same_object)
{
    // Add() returns *this — fluent chaining must work
    VCK::VertexLayout vl;
    VCK::VertexLayout& ref = vl.Add("pos", VCK::VertexAttrType::Vec3);
    ASSERT_TRUE(&ref == &vl);
}

// ---------------------------------------------------------------------------
//  PushConstants [24] — R19: testable with zero GPU init
// ---------------------------------------------------------------------------

TEST(PushConstants_R19, empty_block_size_is_zero)
{
    // R19: default-constructed PushConstants costs nothing.
    VCK::PushConstants pc;
    ASSERT_EQ(pc.Size(), 0u);
}

TEST(PushConstants_R19, float_slot_is_4_bytes)
{
    VCK::PushConstants pc;
    pc.Declare("time", VCK::PushConstType::Float);
    ASSERT_EQ(pc.Size(), 4u);
}

TEST(PushConstants_R19, vec2_slot_is_8_bytes)
{
    VCK::PushConstants pc;
    pc.Declare("offset", VCK::PushConstType::Vec2);
    ASSERT_EQ(pc.Size(), 8u);
}

TEST(PushConstants_R19, vec3_slot_is_12_bytes)
{
    VCK::PushConstants pc;
    pc.Declare("pos", VCK::PushConstType::Vec3);
    ASSERT_EQ(pc.Size(), 12u);
}

TEST(PushConstants_R19, vec4_slot_is_16_bytes)
{
    VCK::PushConstants pc;
    pc.Declare("col", VCK::PushConstType::Vec4);
    ASSERT_EQ(pc.Size(), 16u);
}

TEST(PushConstants_R19, mat4_slot_is_64_bytes)
{
    VCK::PushConstants pc;
    pc.Declare("mvp", VCK::PushConstType::Mat4);
    ASSERT_EQ(pc.Size(), 64u);
}

TEST(PushConstants_R19, int_slot_is_4_bytes)
{
    VCK::PushConstants pc;
    pc.Declare("index", VCK::PushConstType::Int);
    ASSERT_EQ(pc.Size(), 4u);
}

TEST(PushConstants_R19, uint_slot_is_4_bytes)
{
    VCK::PushConstants pc;
    pc.Declare("flags", VCK::PushConstType::UInt);
    ASSERT_EQ(pc.Size(), 4u);
}

TEST(PushConstants_R19, two_mat4_is_128_bytes)
{
    // Classic MVP + model push constant block — must fit spec minimum (128 bytes)
    VCK::PushConstants pc;
    pc.Declare("mvp",   VCK::PushConstType::Mat4)
      .Declare("model", VCK::PushConstType::Mat4);
    ASSERT_EQ(pc.Size(), 128u);
}

TEST(PushConstants_R19, size_accumulates_across_declares)
{
    // float(4) + vec3(12) + mat4(64) = 80
    VCK::PushConstants pc;
    pc.Declare("time",  VCK::PushConstType::Float)
      .Declare("color", VCK::PushConstType::Vec3)
      .Declare("mvp",   VCK::PushConstType::Mat4);
    ASSERT_EQ(pc.Size(), 80u);
}

TEST(PushConstants_R19, range_size_matches_block_size)
{
    // VkPushConstantRange.size must equal pc.Size()
    VCK::PushConstants pc;
    pc.Declare("mvp",   VCK::PushConstType::Mat4)
      .Declare("model", VCK::PushConstType::Mat4);
    auto range = pc.Range(VK_SHADER_STAGE_VERTEX_BIT);
    ASSERT_EQ(range.size,   pc.Size());
    ASSERT_EQ(range.offset, 0u);
}

TEST(PushConstants_R19, range_stage_matches_requested_stage)
{
    VCK::PushConstants pc;
    pc.Declare("mvp", VCK::PushConstType::Mat4);
    auto range = pc.Range(VK_SHADER_STAGE_VERTEX_BIT);
    ASSERT_TRUE((range.stageFlags & VK_SHADER_STAGE_VERTEX_BIT) != 0);
}

TEST(PushConstants_R19, fluent_declare_returns_same_object)
{
    VCK::PushConstants pc;
    VCK::PushConstants& ref = pc.Declare("mvp", VCK::PushConstType::Mat4);
    ASSERT_TRUE(&ref == &pc);
}

TEST(PushConstants_R19, set_float_logs_error_on_unknown_name)
{
    // R14 + R19: Set on an unknown name must emit VCKLog::Error, not silently corrupt
    VCK::Test::LogCapture cap;
    VCK::PushConstants pc;
    pc.Declare("time", VCK::PushConstType::Float);
    pc.Set("WRONG_NAME", 1.0f); // unknown — must log error
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
}

TEST(PushConstants_R19, set_wrong_type_logs_error)
{
    // R14: type mismatch must emit VCKLog::Error
    VCK::Test::LogCapture cap;
    VCK::PushConstants pc;
    pc.Declare("mvp", VCK::PushConstType::Mat4);
    pc.Set("mvp", 1.0f); // declared as Mat4, set as float — type mismatch
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
}

// ---------------------------------------------------------------------------
//  Primitives [25] — R16: no engine assumptions, just geometry data
// ---------------------------------------------------------------------------

TEST(Primitives_R16, cube_has_vertices)
{
    // R16: Primitives returns raw geometry — no scene graph, no transforms,
    // no material. Just positions, normals, uvs, indices.
    auto m = VCK::Primitives::Cube(1.0f);
    ASSERT_TRUE(m.positions.size() > 0);
    ASSERT_TRUE(m.normals.size()   > 0);
    ASSERT_TRUE(m.uvs.size()       > 0);
    ASSERT_TRUE(m.indices.size()   > 0);
}

TEST(Primitives_R16, cube_parallel_arrays)
{
    // Positions, normals, uvs must all have the same count — parallel arrays
    auto m = VCK::Primitives::Cube(1.0f);
    ASSERT_EQ(m.positions.size(), m.normals.size());
    ASSERT_EQ(m.positions.size(), m.uvs.size());
}

TEST(Primitives_R16, cube_indices_are_triangles)
{
    // Indices must be divisible by 3 — triangle list topology
    auto m = VCK::Primitives::Cube(1.0f);
    ASSERT_EQ(m.indices.size() % 3, static_cast<std::size_t>(0));
}

TEST(Primitives_R16, cube_indices_in_bounds)
{
    // Every index must be a valid vertex index
    auto m = VCK::Primitives::Cube(1.0f);
    for (uint32_t idx : m.indices)
        ASSERT_TRUE(idx < static_cast<uint32_t>(m.positions.size()));
}

TEST(Primitives_R16, plane_has_vertices)
{
    auto m = VCK::Primitives::Plane(2.0f, 2.0f);
    ASSERT_TRUE(m.positions.size() > 0);
    ASSERT_TRUE(m.indices.size()   > 0);
}

TEST(Primitives_R16, sphere_has_vertices)
{
    auto m = VCK::Primitives::Sphere(0.5f, 8, 16);
    ASSERT_TRUE(m.positions.size() > 0);
    ASSERT_TRUE(m.indices.size()   > 0);
}

TEST(Primitives_R16, sphere_parallel_arrays)
{
    auto m = VCK::Primitives::Sphere(0.5f, 8, 16);
    ASSERT_EQ(m.positions.size(), m.normals.size());
    ASSERT_EQ(m.positions.size(), m.uvs.size());
}

TEST(Primitives_R16, quad_has_4_vertices_and_6_indices)
{
    // A quad is two triangles — 4 unique vertices, 6 indices
    auto m = VCK::Primitives::Quad();
    ASSERT_EQ(m.positions.size(), static_cast<std::size_t>(4));
    ASSERT_EQ(m.indices.size(),   static_cast<std::size_t>(6));
}

TEST(Primitives_R16, line_has_2_vertices)
{
    VCK::Vec3 a{0, 0, 0}, b{1, 0, 0};
    auto m = VCK::Primitives::Line(a, b);
    ASSERT_EQ(m.positions.size(), static_cast<std::size_t>(2));
}
