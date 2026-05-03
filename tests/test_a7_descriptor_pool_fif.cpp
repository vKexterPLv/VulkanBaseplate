// =============================================================================
//  test_a7_descriptor_pool_fif.cpp
//
//  Theme A · A7 — VulkanDescriptorPool::framesInFlight contract
//
//  Bug B-1 (v0.5 audit): VulkanDescriptorPool used to hardcode
//  MAX_FRAMES_IN_FLIGHT for both VkDescriptorPoolSize::descriptorCount and
//  VkDescriptorPoolCreateInfo::maxSets.  When a user set
//  cfg.sync.framesInFlight = 1 (Lockstep) or any value other than the
//  compile-time max, the pool over-allocated and GetSet(i) silently returned
//  uninitialized handles for i >= framesInFlight.
//
//  v0.5 fix: framesInFlight is now a required Initialize() parameter.
//  The pool sizes its descriptor-set vector to exactly framesInFlight.
//  GetSet(i) for i >= framesInFlight returns VK_NULL_HANDLE and emits
//  VCKLog::Error("DescriptorPool", ...).  VulkanUniformSet<T> reads
//  framesInFlight from the pool, so the UBO ring sizes itself to match
//  without the user threading the value to two places.
//
//  These tests verify the post-fix contract using only the zero-init path
//  (no real VkDevice required) and the bounds-check error semantics.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"

namespace
{
    // Trivially-copyable test UBO type - uniform-set is templated.
    struct TestUbo
    {
        float values[4];
    };
}


// ---------------------------------------------------------------------------
//  VulkanDescriptorPool — default-construct contract (R19 zero cost)
// ---------------------------------------------------------------------------

TEST(A7_DescriptorPool_FIF, default_construct_zero_frames_in_flight)
{
    // R19 + bug B-1: a default-constructed pool reports zero frames-in-flight.
    // Before the fix the pool exposed no getter at all and the internal
    // std::array was always sized MAX_FRAMES_IN_FLIGHT.
    VCK::Test::LogCapture cap;
    VCK::VulkanDescriptorPool pool;

    ASSERT_EQ(pool.GetFramesInFlight(), 0u);
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
}

TEST(A7_DescriptorPool_FIF, default_construct_getset_returns_null_handle)
{
    // Bug B-1: pre-fix GetSet(i) on an un-initialized pool returned the
    // zero-init slot from the fixed std::array, producing VK_NULL_HANDLE
    // by accident.  Post-fix the vector is empty and GetSet does an
    // explicit bounds check + VCKLog::Error.
    VCK::Test::LogCapture cap;
    VCK::VulkanDescriptorPool pool;

    ASSERT_EQ(pool.GetSet(0), VK_NULL_HANDLE);
}

TEST(A7_DescriptorPool_FIF, getset_out_of_range_logs_error)
{
    // R14: every failure emits exactly one VCKLog::Error with the
    // subsystem tag.  GetSet(i) past the ring is a misuse, not a normal
    // path - it must be loud.
    VCK::Test::LogCapture cap;
    VCK::VulkanDescriptorPool pool;

    (void)pool.GetSet(0);

    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("DescriptorPool"));
    ASSERT_CONTAINS(cap.errors()[0].body, "out of range");
}

TEST(A7_DescriptorPool_FIF, getset_far_out_of_range_still_safe)
{
    // R14: bounds check fires for any index past the ring, not just the
    // first invalid one.  Defends against integer overflow + accidental
    // very-large frameIndex misuse.
    VCK::Test::LogCapture cap;
    VCK::VulkanDescriptorPool pool;

    ASSERT_EQ(pool.GetSet(99), VK_NULL_HANDLE);
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
}


// ---------------------------------------------------------------------------
//  VulkanUniformSet<T> — pool-driven sizing contract
// ---------------------------------------------------------------------------

TEST(A7_UniformSet_FIF, default_construct_zero_frames_in_flight)
{
    // R19: default-constructed UBO set reports zero frames-in-flight; the
    // internal buffer + descriptor vectors are empty.
    VCK::Test::LogCapture cap;
    VCK::VulkanUniformSet<TestUbo> ubo;

    ASSERT_EQ(ubo.GetFramesInFlight(), 0u);
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
}

TEST(A7_UniformSet_FIF, default_construct_getset_returns_null_handle)
{
    // Bug B-1 (UBO side): pre-fix GetSet(i) on an un-initialized UBO set
    // returned the zero-init slot from a fixed std::array; post-fix the
    // vector is empty and GetSet does an explicit bounds check + Error.
    VCK::Test::LogCapture cap;
    VCK::VulkanUniformSet<TestUbo> ubo;

    ASSERT_EQ(ubo.GetSet(0), VK_NULL_HANDLE);
}

TEST(A7_UniformSet_FIF, getset_out_of_range_logs_error)
{
    // R14 contract on the UBO side.
    VCK::Test::LogCapture cap;
    VCK::VulkanUniformSet<TestUbo> ubo;

    (void)ubo.GetSet(0);

    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("UniformSet"));
    ASSERT_CONTAINS(cap.errors()[0].body, "out of range");
}

TEST(A7_UniformSet_FIF, write_out_of_range_logs_error_and_is_noop)
{
    // R14: Write past the end is a misuse, not an exception.  It must
    // log an Error and return without dereferencing the missing buffer.
    VCK::Test::LogCapture cap;
    VCK::VulkanUniformSet<TestUbo> ubo;
    TestUbo data{};

    ubo.Write(0, data);   // ring is empty: 0 is already out of range.

    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("UniformSet"));
    ASSERT_CONTAINS(cap.errors()[0].body, "out of range");
}

TEST(A7_UniformSet_FIF, initialize_with_zero_fif_pool_fails_loud)
{
    // Bug B-1 (interaction): if the pool is uninitialized, the UBO set
    // must fail Initialize fast - no vkCreateBuffer calls, exactly one
    // VCKLog::Error.  Verifies the pool is the single source of truth
    // for the frames-in-flight count (the user threads the value into
    // the pool only).
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice         device;
    VCK::VulkanDescriptorPool pool;            // never Initialize()'d
    VCK::VulkanUniformSet<TestUbo> ubo;

    bool ok = ubo.Initialize(device, pool, /*binding=*/0);

    ASSERT_FALSE(ok);
    ASSERT_GE(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("UniformSet"));
    ASSERT_CONTAINS(cap.errors()[0].body, "zero frames-in-flight");
}
