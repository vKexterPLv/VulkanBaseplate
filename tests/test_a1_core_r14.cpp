// =============================================================================
//  test_a1_core_r14.cpp
//
//  Theme A · A1 — core layer Initialize/Shutdown R14 invariant
//
//  R14 — every failure path returns bool AND emits VCKLog::Error with a
//  subsystem tag.  The complementary contract for Shutdown is silence:
//  shutting down a default-constructed (or already-shut-down) object must
//  be a no-op with zero log output, satisfying R19 (zero cost when unused).
//
//  Pre-A1 several core sites returned false silently:
//    - VulkanPipeline::CreateGraphicsPipeline shader-module path
//    - VulkanPipeline::CreateRenderPass / CreatePipelineLayout (VK_CHECK only)
//    - VulkanPipeline::CreateShaderModule
//    - VulkanDevice::PickPhysicalDevice (no suitable GPU)
//    - VulkanSync / VulkanCommand / VulkanSwapchain VK_CHECK sites (logged
//      with the generic "VK_CHECK" tag instead of the subsystem tag the
//      user actually needs to know which Initialize step failed)
//    - VulkanImage view-creation rollback path
//
//  A1 added subsystem-tagged VCKLog::Error before each return false.  These
//  tests verify the post-fix Shutdown contract on every public core class
//  without requiring a real VkDevice (R16 — zero third-party test deps).
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"


// ---------------------------------------------------------------------------
//  Default-construct silence (R19 — no allocation, no log)
// ---------------------------------------------------------------------------

TEST(A1_Core_R14, vulkan_context_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanContext ctx;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_device_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice dev;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_swapchain_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanSwapchain sw;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_command_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanCommand cmd;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_sync_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanSync sync;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_pipeline_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanPipeline pipe;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_buffer_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanBuffer buf;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_image_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanImage img;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  Shutdown idempotency on default-constructed objects (R14 — no false
//  errors on the un-Initialize'd shutdown path; R19 — zero work)
// ---------------------------------------------------------------------------
//
//  Each core class guards its Shutdown body with `if (!m_Device) return;`
//  (or the equivalent), so a never-initialized object's Shutdown must
//  produce zero error output.  Pipeline / Command emit a "Shut down" Info
//  line on the live path; the un-initialized path is silent for both.

TEST(A1_Core_R14, vulkan_swapchain_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanSwapchain sw;
    sw.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_command_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanCommand cmd;
    cmd.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_sync_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanSync sync;
    sync.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_pipeline_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanPipeline pipe;
    pipe.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_buffer_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanBuffer buf;
    buf.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_image_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanImage img;
    img.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_device_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice dev;
    dev.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  Double-Shutdown idempotency — calling Shutdown twice on an
//  un-Initialize'd object must remain a silent no-op.  Several Shutdown
//  bodies clear the m_Device pointer at the end so a second call hits the
//  early-return guard; that guard must not log.
// ---------------------------------------------------------------------------

TEST(A1_Core_R14, vulkan_pipeline_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanPipeline pipe;
    pipe.Shutdown();
    pipe.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_buffer_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanBuffer buf;
    buf.Shutdown();
    buf.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_image_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanImage img;
    img.Shutdown();
    img.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_command_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanCommand cmd;
    cmd.Shutdown();
    cmd.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A1_Core_R14, vulkan_sync_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanSync sync;
    sync.Shutdown();
    sync.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}
