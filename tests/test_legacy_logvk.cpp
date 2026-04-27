// =============================================================================
//  Legacy LogVk("[Tag] body") parsing + level classification.
// =============================================================================
//  The one-arg LogVk form is kept as a thin wrapper around VCK::Log::Emit so
//  pre-rebrand call sites render cleanly without per-line edits.  R14 says
//  error-like tags must escalate to Error level (always visible).  This file
//  verifies the classification table.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"

#include "VulkanHelpers.h"

TEST(LegacyLogVk, parses_bracketed_tag)
{
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(true);                      // Info visible in sink-only mode anyway, but keeps screen clean
    LogVk("[Swapchain] mailbox -> fifo");
    VCKLog::SetDebug(false);

    ASSERT_GE(cap.entries.size(), static_cast<std::size_t>(1));
    const auto& e = cap.entries[0];
    ASSERT_EQ(e.tag,  std::string("Swapchain"));
    ASSERT_EQ(e.body, std::string("mailbox -> fifo"));
}

TEST(LegacyLogVk, error_tag_escalates_to_error_level)
{
    VCK::Test::LogCapture cap;
    LogVk("[ERROR] something failed");
    LogVk("[FAILED] also failed");
    LogVk("[err] short form");

    // All three should classify as Error regardless of cfg.debug.
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(3));
}

TEST(LegacyLogVk, warn_tag_escalates_to_warn_level)
{
    VCK::Test::LogCapture cap;
    LogVk("[WARN] heads-up");
    LogVk("[WARNING] heads-up too");

    ASSERT_EQ(cap.warns().size(), static_cast<std::size_t>(2));
}

TEST(LegacyLogVk, plain_tag_stays_at_info)
{
    VCK::Test::LogCapture cap;
    LogVk("[Context] starting");                 // not error/warn keyword

    ASSERT_GE(cap.entries.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.entries[0].level, VCKLog::Level::Info);
}

TEST(LegacyLogVk, two_arg_form_uses_classification)
{
    VCK::Test::LogCapture cap;
    LogVk("ERR",  "two-arg fail");               // classifies as Error
    LogVk("Info", "two-arg info");               // stays Info

    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].body, std::string("two-arg fail"));
}
