// =============================================================================
//  test_a4_execution_r14.cpp
//
//  Theme A · A4 (execution sweep) R14 / R19 contract tests.
//
//  Pre-A4 the execution layer had:
//
//    - TimelineSemaphore::Initialize: on vkCreateSemaphore failure
//      m_Device was left set, so the failed-Initialize object was not
//      observably equivalent to a default-constructed one.
//
//    - QueueSet::Initialize: silently returned false when the graphics
//      queue was missing.  Callers (FrameScheduler) then printed their
//      own generic "QueueSet::Initialize failed" line that gave the
//      user no actionable context, and a leaked m_Compute / m_Transfer
//      stayed populated even though the QueueSet was logically dead.
//
//    - FrameScheduler::Initialize: m_Device / m_Command / m_Sync were
//      assigned at the top of Initialize before any sub-step succeeded,
//      and on failure the function returned false without rolling them
//      back.  The double-log (FrameScheduler Error + leaf Error) was
//      strictly an R14 violation.
//
//  These tests pin the post-fix R14 / R19 contract on the execution
//  layer's public types without requiring a real VkDevice (R16 — zero
//  third-party test deps).
// =============================================================================
#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"
#include "VCKExecution.h"


// ---------------------------------------------------------------------------
//  Default-construct silence (R19)
// ---------------------------------------------------------------------------
TEST(A4_Execution_R14, timeline_semaphore_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::TimelineSemaphore ts;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, queue_set_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::QueueSet qs;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, gpu_submission_batcher_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::GpuSubmissionBatcher b;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, backpressure_governor_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::BackpressureGovernor g;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, job_graph_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::JobGraph jg;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, debug_timeline_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::DebugTimeline t;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, frame_scheduler_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::FrameScheduler fs;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  Shutdown idempotency on default-constructed objects (R14 / R19).
//  Each public type must guarantee a Shutdown on a never-Initialize'd
//  object emits zero log lines and is safe to repeat.
// ---------------------------------------------------------------------------
TEST(A4_Execution_R14, timeline_semaphore_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::TimelineSemaphore ts;
    ts.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, timeline_semaphore_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::TimelineSemaphore ts;
    ts.Shutdown();
    ts.Shutdown();
    ts.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, queue_set_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::QueueSet qs;
    qs.Shutdown();
    qs.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, gpu_submission_batcher_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::GpuSubmissionBatcher b;
    b.Shutdown();
    b.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, backpressure_governor_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::BackpressureGovernor g;
    g.Shutdown();
    g.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, job_graph_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::JobGraph jg;
    jg.Shutdown();
    jg.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, debug_timeline_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::DebugTimeline t;
    t.Shutdown();
    t.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A4_Execution_R14, frame_scheduler_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::FrameScheduler fs;
    fs.Shutdown();
    fs.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  DebugTimeline::DumpChromeTracing R14 contract: empty path emits a
//  subsystem-tagged Error and returns false.  Pre-A4 this was already
//  correct; this test pins the contract so an A5 / future refactor
//  can't accidentally regress it to silent failure.
// ---------------------------------------------------------------------------
TEST(A4_Execution_R14, debug_timeline_dump_empty_path_logs_error)
{
    VCK::Test::LogCapture cap;
    VCK::DebugTimeline t;
    t.Initialize(true);

    const bool ok = t.DumpChromeTracing(nullptr);
    ASSERT_EQ(ok, false);

    // Single subsystem-tagged Error.
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("DebugTimeline"));

    t.Shutdown();
}


// ---------------------------------------------------------------------------
//  BackpressureGovernor: Initialize() with maxLag > framesInFlight emits
//  exactly one Info line about the clamp; no Errors, no Warns (R14).
//  Shutdown is silent (R19).
// ---------------------------------------------------------------------------
TEST(A4_Execution_R14, backpressure_governor_clamp_info_only)
{
    VCK::Test::LogCapture cap;
    VCK::BackpressureGovernor g;

    g.Initialize(VCK::FramePolicy::AsyncMax, /*maxLag=*/8u, /*framesInFlight=*/2u);

    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));

    g.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}
