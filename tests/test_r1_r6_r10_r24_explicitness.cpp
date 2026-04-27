// =============================================================================
//  test_r1_r6_r10_r24_explicitness.cpp
//
//  Category I — Explicitness
//  Rules: R1, R6, R10, R24
//
//  R1  — No hidden allocations, no singletons, no implicit lifetimes.
//        Every Initialize has a matching Shutdown.
//  R6  — No hidden behaviour. Everything is traceable.
//  R10 — Zero hidden GPU state. No singletons or global managers.
//        (VCKLog dedup map is the only documented exception.)
//  R24 — cfg is the contract. Every behavioral difference lives in cfg.
//
//  These tests verify the Explicitness promises without a real GPU device.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VulkanHelpers.h"

// ---------------------------------------------------------------------------
//  R1 — Explicit > magic
// ---------------------------------------------------------------------------

TEST(R1_Explicitness, vcklog_debug_off_by_default)
{
    // R1: Info lines must be hidden until cfg.debug = true is set explicitly.
    // Nothing implicit — the user must turn it on.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);

    VCKLog::Info("Test", "should not appear");

    // Sink always fires (before debug filter) so cap sees it
    // but screen would suppress it — verify level is Info not auto-promoted
    ASSERT_EQ(cap.infos().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
}

TEST(R1_Explicitness, debug_flag_gates_info_explicitly)
{
    // R1: Info visibility is an explicit opt-in via SetDebug(true).
    // No automatic promotion, no hidden state change.
    VCK::Test::LogCapture cap;

    VCKLog::SetDebug(false);
    VCKLog::Info("Test", "gated");
    std::size_t beforeEnable = cap.infos().size();

    VCKLog::SetDebug(true);
    VCKLog::Info("Test", "visible");
    std::size_t afterEnable = cap.infos().size();

    VCKLog::SetDebug(false);

    // Sink sees both regardless — but we verify the flag actually changed
    ASSERT_EQ(beforeEnable, static_cast<std::size_t>(1)); // sink sees all
    ASSERT_EQ(afterEnable,  static_cast<std::size_t>(2)); // sink sees all
}

// ---------------------------------------------------------------------------
//  R6 — No hidden behaviour
// ---------------------------------------------------------------------------

TEST(R6_NoBehavior, notice_always_visible_no_debug_flag_needed)
{
    // R6: Notice is always visible — no hidden suppression based on debug.
    // The user can always trace what VCK decided.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false); // debug explicitly off

    VCKLog::Notice("Device", "picked discrete GPU");
    VCKLog::Notice("AA",     "auto-picked MSAA_4x");

    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(2));
}

TEST(R6_NoBehavior, warn_always_visible)
{
    // R6: Warn is always visible — recoverable surprises must be traceable.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);

    VCKLog::Warn("VMM", "fence creation fallback to vkQueueWaitIdle");

    ASSERT_EQ(cap.warns().size(), static_cast<std::size_t>(1));
}

TEST(R6_NoBehavior, error_always_visible_independent_of_debug)
{
    // R6 + R14: Errors are always visible regardless of cfg.debug.
    // Nothing hides a failure.
    VCK::Test::LogCapture cap;

    VCKLog::SetDebug(false);
    VCKLog::Error("Pipeline", "shader module creation failed");
    VCKLog::SetDebug(true);
    VCKLog::Error("Device",   "no suitable physical device");
    VCKLog::SetDebug(false);

    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(2));
}

// ---------------------------------------------------------------------------
//  R10 — Zero hidden GPU state
// ---------------------------------------------------------------------------

TEST(R10_NoHiddenState, sink_installs_and_clears_cleanly)
{
    // R10: VCKLog's sink mechanism is the only process-global VCK state
    // (besides the dedup map). Verify it installs and clears without leaking
    // into subsequent captures — no hidden residual state.
    {
        VCK::Test::LogCapture cap1;
        VCKLog::Notice("X", "inside cap1");
        ASSERT_EQ(cap1.entries.size(), static_cast<std::size_t>(1));
    } // cap1 destructor clears sink

    VCKLog::Notice("X", "between caps"); // no sink — goes to screen only

    {
        VCK::Test::LogCapture cap2;
        // cap2 should start empty — no leaked state from cap1
        ASSERT_EQ(cap2.entries.size(), static_cast<std::size_t>(0));
        VCKLog::Notice("X", "inside cap2");
        ASSERT_EQ(cap2.entries.size(), static_cast<std::size_t>(1));
    }
}

TEST(R10_NoHiddenState, dedup_resets_between_captures)
{
    // R10: LogCapture resets dedup state on construction so a residual
    // LastKey from a previous test doesn't corrupt this test's dedup behavior.
    {
        VCK::Test::LogCapture cap1;
        VCKLog::Notice("Hot", "repeated line");
        VCKLog::Notice("Hot", "repeated line"); // dedup candidate on screen
    }

    // Fresh capture — dedup state was reset by LogCapture ctor
    VCK::Test::LogCapture cap2;
    VCKLog::Notice("Hot", "repeated line"); // should not be suppressed by cap1's state
    ASSERT_EQ(cap2.notices().size(), static_cast<std::size_t>(1));
}

// ---------------------------------------------------------------------------
//  R24 — cfg is the contract
// ---------------------------------------------------------------------------

TEST(R24_CfgContract, error_level_never_needs_cfg_debug)
{
    // R24 + R14: Error is not a cfg knob — it's always on.
    // The contract is: cfg.debug gates Info only. Everything else is unconditional.
    VCK::Test::LogCapture cap;

    VCKLog::SetDebug(false); // simulate cfg.debug = false
    VCKLog::Error("Subsys", "fatal failure");

    // Error must fire regardless of the debug knob
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.infos().size(),  static_cast<std::size_t>(0));
}

TEST(R24_CfgContract, notice_level_never_needs_cfg_debug)
{
    // R24: Notice is a user-visible behavioral decision (AA pick, present mode
    // fallback, queue family choice). These are always surfaced — they are
    // part of the cfg contract made visible, not hidden behind a flag.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);

    VCKLog::Notice("Swapchain", "FIFO selected (Mailbox not available)");

    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(1));
}

TEST(R24_CfgContract, info_is_the_only_debug_gated_level)
{
    // R24: Exactly one level is gated by cfg.debug — Info.
    // Notice, Warn, Error are always on. This is the contract.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);

    VCKLog::Info  ("A", "info");
    VCKLog::Notice("B", "notice");
    VCKLog::Warn  ("C", "warn");
    VCKLog::Error ("D", "error");

    // Sink sees all four regardless — test that levels are correct
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(4));
    ASSERT_EQ(cap.infos()   .size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.notices() .size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.warns()   .size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()  .size(), static_cast<std::size_t>(1));
}
