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
#include <ostream>
#include <string>
#include <vector>

// ASSERT_EQ in vck_test.h streams both operands into an ostringstream when
// the comparison fails, so any type used with ASSERT_EQ must be streamable.
// VCKLog::Level is an `enum class` with no implicit conversion - provide a
// streaming overload here, in the same namespace as the enum so ADL finds
// it from any TU that already includes vck_log_capture.h.
namespace VCK { namespace Log {
inline std::ostream& operator<<(std::ostream& os, Level lvl)
{
    switch (lvl) {
        case Level::Info:   return os << "Info";
        case Level::Notice: return os << "Notice";
        case Level::Warn:   return os << "Warn";
        case Level::Error:  return os << "Error";
    }
    return os << "Level(?)";
}
}} // namespace VCK::Log

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
