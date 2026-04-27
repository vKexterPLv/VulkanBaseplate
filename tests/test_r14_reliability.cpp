// =============================================================================
//  test_r14_reliability.cpp
//
//  Category V — Reliability
//  Rule: R14
//
//  R14 — Fail fast, fail loud.
//        Every failure returns an explicit bool AND emits exactly one
//        VCKLog::Error with a subsystem tag.
//        A return false without a VCKLog::Error is a bug.
//        VK_CHECK routes non-VK_SUCCESS to VCKLog::Error regardless of
//        cfg.debug.
//
//  This file extends the basic VK_CHECK tests with deeper R14 contract
//  verification — subsystem tag format, error-only-once guarantee,
//  and debug-independence.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VulkanHelpers.h"
#include <vulkan/vulkan.h>

// ---------------------------------------------------------------------------
//  VK_CHECK contract (R14 primary surface)
// ---------------------------------------------------------------------------

TEST(R14_FailFastLoud, vk_check_success_silent)
{
    // R14: success must produce zero noise — no log, no side effects.
    VCK::Test::LogCapture cap;
    bool ok = VK_CHECK(VkResult{VK_SUCCESS});
    ASSERT_TRUE(ok);
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(R14_FailFastLoud, vk_check_failure_returns_false)
{
    // R14: failure must return false so the caller can react.
    VCK::Test::LogCapture cap;
    bool ok = VK_CHECK(VkResult{VK_ERROR_INITIALIZATION_FAILED});
    ASSERT_FALSE(ok);
}

TEST(R14_FailFastLoud, vk_check_failure_emits_exactly_one_error)
{
    // R14: one failure = one VCKLog::Error. Not zero, not two.
    VCK::Test::LogCapture cap;
    (void)VK_CHECK(VkResult{VK_ERROR_DEVICE_LOST});
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
}

TEST(R14_FailFastLoud, vk_check_tag_is_vk_check)
{
    // R14: the subsystem tag must be "VK_CHECK" so log output is scannable.
    VCK::Test::LogCapture cap;
    (void)VK_CHECK(VkResult{VK_ERROR_OUT_OF_HOST_MEMORY});
    ASSERT_EQ(cap.errors()[0].tag, std::string("VK_CHECK"));
}

TEST(R14_FailFastLoud, vk_check_body_contains_result_name)
{
    // R14: the error body must identify what failed — result name in body.
    VCK::Test::LogCapture cap;
    (void)VK_CHECK(VkResult{VK_ERROR_OUT_OF_DEVICE_MEMORY});
    ASSERT_CONTAINS(cap.errors()[0].body, "VK_ERROR_OUT_OF_DEVICE_MEMORY");
}

TEST(R14_FailFastLoud, vk_check_fires_regardless_of_debug_flag)
{
    // R14: errors are NOT gated by cfg.debug. Always loud.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);
    (void)VK_CHECK(VkResult{VK_ERROR_INITIALIZATION_FAILED});
    VCKLog::SetDebug(false);
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
}

TEST(R14_FailFastLoud, vk_check_multiple_distinct_failures_each_log_once)
{
    // R14: N distinct failures → N error log lines. No suppression.
    VCK::Test::LogCapture cap;
    (void)VK_CHECK(VkResult{VK_ERROR_OUT_OF_HOST_MEMORY});
    (void)VK_CHECK(VkResult{VK_ERROR_OUT_OF_DEVICE_MEMORY});
    (void)VK_CHECK(VkResult{VK_ERROR_DEVICE_LOST});
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(3));
}

TEST(R14_FailFastLoud, vk_check_repeated_same_failure_each_log_once)
{
    // R14: same failure repeated 3x → 3 error entries in sink.
    // Dedup only affects screen output, not the sink.
    VCK::Test::LogCapture cap;
    (void)VK_CHECK(VkResult{VK_ERROR_DEVICE_LOST});
    (void)VK_CHECK(VkResult{VK_ERROR_DEVICE_LOST});
    (void)VK_CHECK(VkResult{VK_ERROR_DEVICE_LOST});
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(3));
}

// ---------------------------------------------------------------------------
//  VCKLog::Error direct contract (R14 for non-VK_CHECK paths)
// ---------------------------------------------------------------------------

TEST(R14_FailFastLoud, direct_error_has_tag_and_body)
{
    // R14: every VCKLog::Error must carry a subsystem tag.
    // Tag-less errors are not allowed — the rule requires "VCKLog::Error(tag, ...)"
    VCK::Test::LogCapture cap;
    VCKLog::Error("Pipeline", "shader module returned VK_NULL_HANDLE");

    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("Pipeline"));
    ASSERT_CONTAINS(cap.errors()[0].body, "shader module");
}

TEST(R14_FailFastLoud, error_tag_is_non_empty)
{
    // R14: subsystem tag must be non-empty. An empty tag means the error
    // is not attributable to a subsystem — that violates the rule.
    VCK::Test::LogCapture cap;
    VCKLog::Error("Device", "no discrete GPU found");

    ASSERT_FALSE(cap.errors()[0].tag.empty());
}

TEST(R14_FailFastLoud, error_body_is_non_empty)
{
    // R14: error body must carry a meaningful message. Empty body = silent failure.
    VCK::Test::LogCapture cap;
    VCKLog::Error("Swapchain", "recreate failed: extent is zero");

    ASSERT_FALSE(cap.errors()[0].body.empty());
}

// ---------------------------------------------------------------------------
//  R14 + R6: errors never get accidentally demoted to Info/Notice/Warn
// ---------------------------------------------------------------------------

TEST(R14_FailFastLoud, error_level_not_demoted)
{
    // R14 + R6: an Error must arrive at the sink as Error, not Info.
    // No accidental level demotion in the logging path.
    VCK::Test::LogCapture cap;
    VCKLog::Error("Command", "begin recording failed");

    ASSERT_EQ(cap.errors().size(),  static_cast<std::size_t>(1));
    ASSERT_EQ(cap.infos().size(),   static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),   static_cast<std::size_t>(0));
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(0));
}
