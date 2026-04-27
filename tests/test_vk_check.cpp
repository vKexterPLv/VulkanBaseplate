// =============================================================================
//  R14 / VK_CHECK behaviour.
// =============================================================================
//  Rule 14: every failure returns bool AND emits exactly one
//  VCKLog::Error("<subsystem>", ...) line.  VK_CHECK is the most heavily
//  used surface that enforces this contract; we test it here without needing
//  a real Vulkan device by feeding literal VkResult values.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"

#include "VulkanHelpers.h"
#include <vulkan/vulkan.h>

TEST(VkCheck, success_returns_true_no_log)
{
    VCK::Test::LogCapture cap;
    bool ok = VK_CHECK(VkResult{VK_SUCCESS});
    ASSERT_TRUE(ok);
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(VkCheck, failure_returns_false_and_emits_one_error)
{
    VCK::Test::LogCapture cap;
    bool ok = VK_CHECK(VkResult{VK_ERROR_INITIALIZATION_FAILED});

    ASSERT_FALSE(ok);
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    // The macro stringifies its argument so the body should mention the
    // expression text plus the numeric VkResult code.
    ASSERT_EQ(cap.errors()[0].tag, std::string("VK_CHECK"));
    ASSERT_CONTAINS(cap.errors()[0].body, "VK_ERROR_INITIALIZATION_FAILED");
}

TEST(VkCheck, failure_logs_error_even_when_debug_is_off)
{
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);                     // R14: errors are NOT debug-gated
    (void)VK_CHECK(VkResult{VK_ERROR_DEVICE_LOST});
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
}

TEST(VkCheck, distinct_failures_each_log_once)
{
    VCK::Test::LogCapture cap;
    (void)VK_CHECK(VkResult{VK_ERROR_OUT_OF_HOST_MEMORY});
    (void)VK_CHECK(VkResult{VK_ERROR_OUT_OF_DEVICE_MEMORY});
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(2));
}
