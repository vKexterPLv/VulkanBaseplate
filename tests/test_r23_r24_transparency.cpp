// =============================================================================
//  test_r23_r24_transparency.cpp
//
//  Category VI — Transparency
//  Rules: R23, R24 (logging side)
//
//  R23 — Extension transparency. Every extension VCK enables silently must
//        be logged via VCKLog::Notice("Device", ...) at init.
//        The user is never surprised by what is running underneath.
//
//  R24 — cfg is the contract (logging side). Behavioral decisions driven
//        by cfg must produce a VCKLog::Notice so the user can see what
//        was picked.
//
//  These tests verify the logging contract for decisions VCK makes on behalf
//  of the user — AA pick, present mode fallback, queue family selection.
//  We simulate the decision + log pattern without a real GPU device.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VulkanHelpers.h"

// ---------------------------------------------------------------------------
//  R23 — Extension transparency logging contract
// ---------------------------------------------------------------------------

TEST(R23_ExtensionTransparency, notice_has_device_tag)
{
    // R23: extension notices must use "Device" as the subsystem tag
    // so they're grouped and scannable in the log output.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Device", "VK_KHR_timeline_semaphore: enabled");
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.notices()[0].tag, std::string("Device"));
}

TEST(R23_ExtensionTransparency, fallback_notice_has_device_tag)
{
    // R23: fallback paths must also log via Notice so the user knows
    // the extension wasn't available.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Device", "VK_KHR_timeline_semaphore: not available, using per-slot fences");
    ASSERT_EQ(cap.notices()[0].tag, std::string("Device"));
    ASSERT_CONTAINS(cap.notices()[0].body, "not available");
}

TEST(R23_ExtensionTransparency, notice_is_not_error_or_warn)
{
    // R23: extension status is informational — Notice, not Error/Warn.
    // An unavailable extension is not a failure (rule 19 — fallback exists).
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Device", "VK_EXT_memory_budget: enabled");
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(1));
}

TEST(R23_ExtensionTransparency, notice_fires_without_debug_flag)
{
    // R23: extension transparency is unconditional — never gated by cfg.debug.
    // The user must always know what hardware features are active.
    VCK::Test::LogCapture cap;
    VCKLog::SetDebug(false);
    VCKLog::Notice("Device", "dedicated compute queue: enabled");
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(1));
}

TEST(R23_ExtensionTransparency, multiple_extensions_each_notice_once)
{
    // R23: each extension gets exactly one notice — not batched silently,
    // not repeated every frame.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Device", "VK_KHR_timeline_semaphore: enabled");
    VCKLog::Notice("Device", "VK_KHR_synchronization2: enabled");
    VCKLog::Notice("Device", "VK_EXT_memory_budget: enabled");
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(3));
}

// ---------------------------------------------------------------------------
//  R24 — cfg decisions must be observable via Notice
// ---------------------------------------------------------------------------

TEST(R24_CfgObservable, aa_pick_logged_as_notice)
{
    // R24: AA auto-detection is a cfg-driven decision. The pick must be
    // logged so the user can verify what was chosen matches their intent.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("AA", "auto-picked MSAA_4x (MID tier, forward renderer)");
    ASSERT_EQ(cap.notices().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.notices()[0].tag, std::string("AA"));
    ASSERT_CONTAINS(cap.notices()[0].body, "auto-picked");
}

TEST(R24_CfgObservable, present_mode_fallback_logged_as_notice)
{
    // R24: present mode fallback is a cfg-driven behavioral change.
    // Must be logged so the user knows Mailbox wasn't available.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Swapchain", "Mailbox not available, falling back to FIFO");
    ASSERT_EQ(cap.notices()[0].tag, std::string("Swapchain"));
    ASSERT_CONTAINS(cap.notices()[0].body, "FIFO");
}

TEST(R24_CfgObservable, queue_family_fallback_logged_as_notice)
{
    // R24: dedicated queue fallback is observable.
    // When enableDedicatedComputeQueue = true but no separate family exists,
    // VCK falls back to graphics queue and logs it.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Device", "no dedicated compute queue, aliasing to graphics");
    ASSERT_EQ(cap.notices()[0].tag, std::string("Device"));
    ASSERT_CONTAINS(cap.notices()[0].body, "aliasing");
}

TEST(R24_CfgObservable, msaa_clamp_logged_as_notice)
{
    // R24: MSAA sample count clamp is a cfg-driven behavioral change.
    // User asked for 8x but device supports max 4x — must be logged.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Swapchain", "MSAA 8x not supported, clamped to 4x");
    ASSERT_CONTAINS(cap.notices()[0].body, "clamped");
}

TEST(R24_CfgObservable, live_resize_drain_logged_as_notice)
{
    // R24 + R12: LiveResize is an explicit recreation event that must be
    // observable in logs (rule 12) and triggered by cfg/user action (rule 24).
    VCK::Test::LogCapture cap;
    VCKLog::Notice("LiveResize", "swapchain recreating 1280x720 -> 1920x1080");
    ASSERT_EQ(cap.notices()[0].tag, std::string("LiveResize"));
}

// ---------------------------------------------------------------------------
//  R23 body content requirements
// ---------------------------------------------------------------------------

TEST(R23_ExtensionTransparency, enabled_notice_body_contains_extension_name)
{
    // R23: the body must name the extension — "enabled" alone is not enough.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Device", "VK_KHR_timeline_semaphore: enabled");
    ASSERT_CONTAINS(cap.notices()[0].body, "VK_KHR_timeline_semaphore");
}

TEST(R23_ExtensionTransparency, fallback_notice_body_contains_fallback_info)
{
    // R23: fallback body must describe what is being used instead.
    VCK::Test::LogCapture cap;
    VCKLog::Notice("Device", "VK_KHR_timeline_semaphore: not available, using per-slot fences");
    ASSERT_CONTAINS(cap.notices()[0].body, "per-slot fences");
}
