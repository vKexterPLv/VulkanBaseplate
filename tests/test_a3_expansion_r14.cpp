// =============================================================================
//  test_a3_expansion_r14.cpp
//
//  Theme A · A3 (expansion sweep) R14 / R19 contract tests.
//
//  Pre-A3 the expansion layer had:
//    - VulkanOneTimeCommand::Begin / End: VK_CHECK silent failure paths
//      (no subsystem-tagged Error - the only log was VK_CHECK's generic
//      'VK_CHECK' line which is invisible to anyone grepping the
//      'OneTime' tag).
//    - VulkanFramebufferSet::CreateAll: VK_CHECK silent failure on
//      vkCreateFramebuffer + partial-init leak (framebuffers in [0, i)
//      stranded when image i failed) - caller had no reliable way to
//      detect the partial state and call Shutdown.
//    - VulkanModelPipeline::Initialize: partial-init leak (Set0Layout +
//      Set1Layout + PipelineLayout could be created and then leak when a
//      later Build* sub-step failed) and silent VK_CHECK on
//      vkCreatePipelineLayout (VK_CHECK's generic line, no
//      ModelPipeline-tagged Error).
//    - VulkanModelPipeline::BuildDescriptorLayouts: Set0Layout leaked if
//      Set1Layout build failed.
//
//  A3 fixed all of the above.  These tests pin the post-fix R14 / R19
//  contract on the expansion-layer types without requiring a real
//  VkDevice (R16 — zero third-party test deps).
// =============================================================================
#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"
#include "VCKExpansion.h"


// ---------------------------------------------------------------------------
//  Default-construct silence (R19 — no allocation, no log)
// ---------------------------------------------------------------------------
TEST(A3_Expansion_R14, vulkan_framebuffer_set_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanFramebufferSet fbs;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_depth_buffer_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDepthBuffer db;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_sampler_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanSampler s;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_texture_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanTexture t;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_mesh_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanMesh m;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_model_pipeline_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanModelPipeline mp;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_one_time_command_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanOneTimeCommand otc;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  Shutdown idempotency on default-constructed objects (R14 / R19)
//
//  Each public expansion class guards Shutdown with `if (!m_Device) return;`
//  (or equivalent), so a never-Initialize'd object's Shutdown produces zero
//  log output and a double-Shutdown is also silent.
// ---------------------------------------------------------------------------
TEST(A3_Expansion_R14, vulkan_framebuffer_set_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanFramebufferSet fbs;
    fbs.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_framebuffer_set_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanFramebufferSet fbs;
    fbs.Shutdown();
    fbs.Shutdown();
    fbs.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_depth_buffer_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDepthBuffer db;
    db.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_depth_buffer_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDepthBuffer db;
    db.Shutdown();
    db.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_sampler_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanSampler s;
    s.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_texture_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanTexture t;
    t.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_mesh_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanMesh m;
    m.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_model_pipeline_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanModelPipeline mp;
    mp.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}

TEST(A3_Expansion_R14, vulkan_model_pipeline_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanModelPipeline mp;
    mp.Shutdown();
    mp.Shutdown();
    mp.Shutdown();
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(0));
    ASSERT_EQ(cap.warns().size(),  static_cast<std::size_t>(0));
}
