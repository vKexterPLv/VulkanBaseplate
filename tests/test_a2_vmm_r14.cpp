// =============================================================================
//  test_a2_vmm_r14.cpp
//
//  Theme A · A2 (VMM sweep) R14 / R19 contract tests.
//
//  Pre-A2 the VMM had:
//    - Stale "vkQueueWaitIdle model" comments in the header + implementation
//      that didn't match the v0.3 fence-based reality.
//    - VK_CHECK + VCKLog::Error double-logging on three failure paths
//      (transfer command pool, acquire-cmd alloc / begin / end).
//    - Silent failure paths in EnsureStagingCmd (vkAllocateCommandBuffers /
//      vkBeginCommandBuffer just returned false with no subsystem-tagged
//      Error - violates R14).
//    - Partial-init leaks: if transient block N failed, blocks [0, N) +
//      staging ring + transfer command pool would all leak (caller had no
//      reliable way to detect the partial state and call Shutdown).
//
//  A2 fixed all of the above.  These tests pin the post-fix R14 / R19
//  contract on the VMM-owned types without requiring a real VkDevice
//  (R16 — zero third-party test deps).
// =============================================================================
#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"
#include "VulkanMemoryManager.h"


// ---------------------------------------------------------------------------
//  Default-construct silence (R19 — no allocation, no log)
// ---------------------------------------------------------------------------
TEST(A2_VMM_R14, vulkan_memory_manager_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanMemoryManager vmm;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  Shutdown idempotency on default-constructed objects (R14 / R19)
//
//  Shutdown body is guarded by `if (!m_Device) return;`, so a never-
//  initialized VMM's Shutdown must produce zero log output.
// ---------------------------------------------------------------------------
TEST(A2_VMM_R14, vulkan_memory_manager_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanMemoryManager vmm;
    vmm.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  Double-Shutdown idempotency (R14 — second Shutdown does nothing, no log)
// ---------------------------------------------------------------------------
TEST(A2_VMM_R14, vulkan_memory_manager_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanMemoryManager vmm;
    vmm.Shutdown();
    vmm.Shutdown();
    vmm.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  FlushStaging on a never-Initialize'd VMM is silent.  Pre-A2 this would
//  have hit `if (m_StagingOpen)` (= false) and returned silently anyway,
//  but the test pins the contract so a future change can't accidentally
//  introduce a side-effect on a moribund VMM.
// ---------------------------------------------------------------------------
TEST(A2_VMM_R14, vulkan_memory_manager_flush_staging_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanMemoryManager vmm;
    vmm.FlushStaging();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}
