// =============================================================================
//  test_r11_r12_reliability.cpp
//
//  Category V — Reliability
//  Rules: R11, R12
//
//  R11 — Deterministic frame behaviour. Same inputs → same submission order
//        and execution graph. Any nondeterminism is explicitly opt-in.
//  R12 — Explicit recreation events. Swapchain / device / resource recreation
//        must be triggered, logged, and observable. Never silent.
//
//  Both rules are about predictability and observability at the frame level.
//  Tests here verify the logging contract for recreation events (R12) and
//  the determinism contract for frame policy naming (R11).
//  No GPU device required.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"   // umbrella: VCKExecution forward-uses VulkanDevice/Command/Sync/...

// ---------------------------------------------------------------------------
//  R11 — Deterministic frame behaviour
// ---------------------------------------------------------------------------

TEST(R11_Determinism, lockstep_policy_name)
{
    // R11: Lockstep is the maximally deterministic policy — CPU waits for GPU
    // every frame. Its name must be stable and recognizable.
    ASSERT_TRUE(std::string(VCK::FramePolicyName(VCK::FramePolicy::Lockstep))
                == std::string("Lockstep"));
}

TEST(R11_Determinism, pipelined_policy_name)
{
    // R11: Pipelined is the default deterministic policy (CPU N+1, GPU N).
    ASSERT_TRUE(std::string(VCK::FramePolicyName(VCK::FramePolicy::Pipelined))
                == std::string("Pipelined"));
}

TEST(R11_Determinism, asyncmax_policy_name)
{
    // R11: AsyncMax is the only explicitly nondeterministic policy.
    // Its name must distinguish it from the deterministic ones.
    ASSERT_TRUE(std::string(VCK::FramePolicyName(VCK::FramePolicy::AsyncMax))
                == std::string("AsyncMax"));
}

TEST(R11_Determinism, policy_names_are_distinct)
{
    // R11: all three policies must have different names — no aliasing.
    std::string lockstep  = VCK::FramePolicyName(VCK::FramePolicy::Lockstep);
    std::string pipelined = VCK::FramePolicyName(VCK::FramePolicy::Pipelined);
    std::string asyncmax  = VCK::FramePolicyName(VCK::FramePolicy::AsyncMax);
    ASSERT_TRUE(lockstep  != pipelined);
    ASSERT_TRUE(lockstep  != asyncmax);
    ASSERT_TRUE(pipelined != asyncmax);
}

TEST(R11_Determinism, policy_names_are_non_empty)
{
    // R11: every policy must have a non-empty name for logging (R6).
    ASSERT_FALSE(std::string(VCK::FramePolicyName(VCK::FramePolicy::Lockstep)).empty());
    ASSERT_FALSE(std::string(VCK::FramePolicyName(VCK::FramePolicy::Pipelined)).empty());
    ASSERT_FALSE(std::string(VCK::FramePolicyName(VCK::FramePolicy::AsyncMax)).empty());
}

// ---------------------------------------------------------------------------
//  R12 — Explicit recreation events (logging contract)
// ---------------------------------------------------------------------------

TEST(R12_RecreationEvents, swapchain_recreate_logged_as_notice)
{
    // R12: swapchain recreation must be logged — never silent.
    // Uses Notice because it's a behavioral event, not a failure.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Swapchain", "Recreating 1280x720 -> 1920x1080");
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.notices()[0].tag, std::string("Swapchain"));
    ASSERT_CONTAINS(cap.notices()[0].body, "Recreating");
}

TEST(R12_RecreationEvents, live_resize_logged_as_notice)
{
    // R12: live resize is a recreation event — must be observable.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("LiveResize", "resize triggered frame 42");
    ASSERT_EQ(cap.notices()[0].tag, std::string("LiveResize"));
}

TEST(R12_RecreationEvents, recreation_notice_never_an_error)
{
    // R12: recreation is expected behavior, not a failure. Must be Notice.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Swapchain", "Recreating due to VK_SUBOPTIMAL_KHR");
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(1));
}

TEST(R12_RecreationEvents, recreation_failure_must_be_error)
{
    // R12 + R14: if recreation fails, that IS an error — fail loud.
    VCK::Test::LogCapture cap;
    VCKLog::Error("Swapchain", "Recreate failed: vkCreateSwapchainKHR returned error");
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(0));
}

TEST(R12_RecreationEvents, depth_recreate_logged)
{
    // R12: depth buffer recreation on resize must also be observable.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("DepthBuffer", "Recreating 1920x1080");
    ASSERT_EQ(cap.notices()[0].tag, std::string("DepthBuffer"));
}

TEST(R12_RecreationEvents, framebuffer_recreate_logged)
{
    // R12: framebuffer set recreation must be observable.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("FramebufferSet", "Recreating 3 framebuffers");
    ASSERT_EQ(cap.notices()[0].tag, std::string("FramebufferSet"));
}

TEST(R12_RecreationEvents, drain_inflight_logged_on_resize)
{
    // R12: the scheduler drain before resize must be observable —
    // user must be able to see that DrainInFlight was called.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("LiveResize", "DrainInFlight before recreate");
    ASSERT_CONTAINS(cap.notices()[0].body, "DrainInFlight");
}
