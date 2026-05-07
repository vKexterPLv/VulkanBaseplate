// =============================================================================
//  test_s1_sync2.cpp
//
//  Theme S - synchronization2 adoption (S1).  VulkanDevice gains:
//    - cfg.device.preferSync2  (Config::DeviceCfg, default true)
//    - HasSynchronization2()   accessor
//    - tri-state R23 Notice    (enabled / unavailable / disabled-by-cfg)
//
//  These tests cover the parts that do NOT need a real VkPhysicalDevice:
//    1. Config default value (preferSync2 = true).
//    2. Default-construct VulkanDevice silent + HasSynchronization2() = false
//       (R19 / R14: zero state until Initialize, no GPU, no log).
//    3. Shutdown on un-Initialize'd VulkanDevice silent (R19).
//
//  The actual feature toggle and tri-state Notice are exercised by the
//  example R14 harness during CI build of the examples; on this CPU-only
//  test path we just pin the contract that controls those code branches.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"


TEST(S1_Sync2, config_default_prefers_sync2)
{
    VCK::Test::LogCapture cap;
    VCK::Config cfg;
    ASSERT_EQ(cfg.device.preferSync2, true);
    // Reading the default-constructed cfg must not emit anything.
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(S1_Sync2, default_construct_device_reports_no_sync2)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice dev;
    // Pre-Initialize the flag must be false so callers do not record
    // sync2 commands against a device that was never created.
    ASSERT_EQ(dev.HasSynchronization2(), false);
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(S1_Sync2, shutdown_on_uninitialized_device_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice dev;
    dev.Shutdown();  // R19: no allocation -> no work to undo -> silent
    ASSERT_EQ(dev.HasSynchronization2(), false);
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(S1_Sync2, double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice dev;
    dev.Shutdown();
    dev.Shutdown();
    ASSERT_EQ(dev.HasSynchronization2(), false);
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(S1_Sync2, cfg_pin_to_legacy_path_compiles_and_holds)
{
    VCK::Test::LogCapture cap;
    VCK::Config cfg;
    cfg.device.preferSync2 = false;
    // The flag is plumbed end-to-end; user can pin to the legacy 1.0 barrier
    // path with one cfg line.  Reading the post-modification cfg must still
    // be silent (no log on a pure cfg edit; Notice fires from Initialize).
    ASSERT_EQ(cfg.device.preferSync2, false);
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}
