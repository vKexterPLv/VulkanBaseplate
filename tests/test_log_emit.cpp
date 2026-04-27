// =============================================================================
//  Sanity checks on the VCKLog sink hook itself.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"

#include "VulkanHelpers.h"

TEST(LogSink, captures_basic_levels)
{
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(true);                      // make Info visible too

    VCKLog::Info  ("Sub", "info-line");
    VCKLog::Notice("Sub", "notice-line");
    VCKLog::Warn  ("Sub", "warn-line");
    VCKLog::Error ("Sub", "error-line");

    VCKLog::SetDebug(false);

    ASSERT_EQ(cap.entries.size(),  static_cast<std::size_t>(4));
    ASSERT_EQ(cap.entries[0].level, VCKLog::Level::Info);
    ASSERT_EQ(cap.entries[1].level, VCKLog::Level::Notice);
    ASSERT_EQ(cap.entries[2].level, VCKLog::Level::Warn);
    ASSERT_EQ(cap.entries[3].level, VCKLog::Level::Error);
}

TEST(LogSink, sees_calls_before_dedup_and_debug_filter)
{
    // Sink fires unconditionally before filtering, so we should see every
    // call even when Info is suppressed for screen output and even when
    // dedup would suppress repeat lines.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);                     // Info suppressed on screen

    VCKLog::Info ("Repeat", "same");
    VCKLog::Info ("Repeat", "same");             // would dedup on screen
    VCKLog::Error("Repeat", "boom");
    VCKLog::Error("Repeat", "boom");             // would dedup on screen

    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(4));
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(2));
    ASSERT_EQ(cap.infos().size(),  static_cast<std::size_t>(2));
}

TEST(LogSink, cleared_on_capture_dtor)
{
    {
        VCK::Test::LogCapture cap;
        VCKLog::Notice("X", "captured");
        ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(1));
    }
    // After cap goes out of scope the sink must be unset, so the next call
    // should not push into any vector.  Re-installing a fresh capture and
    // verifying it has no leaked entries is the cheapest way to assert it.
    VCKLog::Notice("X", "after-dtor");           // emits to screen, no sink

    VCK::Test::LogCapture cap2;
    ASSERT_EQ(cap2.entries.size(), static_cast<std::size_t>(0));
}
