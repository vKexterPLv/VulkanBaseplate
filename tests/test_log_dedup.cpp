// =============================================================================
//  VCKLog dedup behaviour (screen output) - sink sees every call.
// =============================================================================
//  Dedup happens AFTER the sink, so the harness sees every Emit even when
//  the screen would suppress duplicates.  This test verifies the sink can
//  observe a tight repeat loop without losing any entry, which is the
//  primary value-add for R14 introspection.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"

#include "VulkanHelpers.h"

TEST(LogDedup, sink_sees_repeats_even_when_screen_dedups)
{
    VCK::Test::LogCapture cap;

    for (int i = 0; i < 10; ++i) {
        VCKLog::Notice("Hot", "frame tick");
    }

    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(10));
}

TEST(LogDedup, distinct_lines_all_pass_through)
{
    VCK::Test::LogCapture cap;
    VCKLog::Notice("A", "one");
    VCKLog::Notice("B", "two");
    VCKLog::Notice("A", "one");                  // same as first - sink still sees
    VCKLog::Notice("C", "three");

    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(4));
}
