// =============================================================================
//  VCKLog capture helper for the R14 unit-test harness
// =============================================================================
//  Wraps VCKLog::SetSink so a test scope captures every Emit() call into a
//  vector for after-the-fact assertions.  RAII so a failing assertion before
//  ClearSink is reached doesn't leak the sink into the next test case.
//
//      TEST(Logging, error_routes_to_sink) {
//          VCK::Test::LogCapture cap;
//          VCKLog::Error("Subsys", "boom");
//          ASSERT_EQ(cap.errors().size(), 1);
//          ASSERT_EQ(cap.entries[0].tag,  std::string("Subsys"));
//          ASSERT_CONTAINS(cap.entries[0].body, "boom");
//      }
// =============================================================================

#pragma once

#include "VulkanHelpers.h"
#include <string>
#include <vector>

namespace VCK { namespace Test {

struct LogEntry {
    VCKLog::Level level;
    std::string   tag;
    std::string   body;
};

struct LogCapture {
    std::vector<LogEntry> entries;

    LogCapture() {
        // Reset dedup state so a residual LastKey from a previous test
        // doesn't suppress the first line of this one.
        VCKLog::LastKey().clear();
        VCKLog::DedupCount() = 0;

        VCKLog::SetSink([this](VCKLog::Level lvl,
                                const char*    tag,
                                const std::string& body) {
            entries.push_back({lvl, tag ? tag : "", body});
        });
    }

    ~LogCapture() {
        VCKLog::ClearSink();
    }

    LogCapture(const LogCapture&)            = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    // Convenience filters.
    std::vector<LogEntry> errors() const { return at_level(VCKLog::Level::Error); }
    std::vector<LogEntry> warns()  const { return at_level(VCKLog::Level::Warn); }
    std::vector<LogEntry> notices() const { return at_level(VCKLog::Level::Notice); }
    std::vector<LogEntry> infos()  const { return at_level(VCKLog::Level::Info); }

    std::vector<LogEntry> at_level(VCKLog::Level lvl) const {
        std::vector<LogEntry> out;
        for (const auto& e : entries) if (e.level == lvl) out.push_back(e);
        return out;
    }

    bool any_at_level(VCKLog::Level lvl) const {
        for (const auto& e : entries) if (e.level == lvl) return true;
        return false;
    }
};

}} // namespace VCK::Test
