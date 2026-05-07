// =============================================================================
//  test_e1_dynamic_rendering.cpp
//
//  Theme E · E1 — VK_KHR_dynamic_rendering / 1.3 core path
//
//  E1 ships the dynamic-rendering codepath end-to-end:
//    - VulkanDevice probes VkPhysicalDeviceDynamicRenderingFeatures and
//      chains it into vkCreateDevice when cfg.rendering.mode = Dynamic
//      AND the device advertises the feature.
//    - VulkanDevice exposes HasDynamicRendering() and GetRenderingMode()
//      so VulkanPipeline can choose VkRenderPass + VkFramebuffer (Classic)
//      vs vkCmdBeginRendering (Dynamic) without a cfg round-trip.
//    - VulkanPipeline gates CreateRenderPass on HasDynamicRendering();
//      when true it skips the render pass and chains
//      VkPipelineRenderingCreateInfo into vkCreateGraphicsPipelines.
//    - R23 tri-state Notice (active / unavailable / cfg=Classic) fires
//      from VulkanDevice::Initialize so users can grep the log.
//
//  These tests pin the public-API contract on a default-constructed
//  VulkanDevice / VulkanPipeline (R19 — zero allocations, zero log).
//  The actual feature-probe / extension-request / pipeline pNext chain
//  is only reachable inside Initialize against a real VkDevice and is
//  exercised by the example menu's RGBTriangle target on real GPUs.
// =============================================================================

#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"


// ---------------------------------------------------------------------------
//  R19 — default-constructed VulkanDevice reports HasDynamicRendering()=false
//          and GetRenderingMode()=Classic, with zero log output.
// ---------------------------------------------------------------------------

TEST(E1_DynamicRendering, device_default_construct_no_dynamic_rendering)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice dev;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
    ASSERT_FALSE(dev.HasDynamicRendering());
}

TEST(E1_DynamicRendering, device_default_construct_rendering_mode_classic)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanDevice dev;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
    ASSERT_TRUE(dev.GetRenderingMode() == VCK::RenderingMode::Classic);
}


// ---------------------------------------------------------------------------
//  R19 — default-constructed VulkanPipeline reports UsesDynamicRendering()=false,
//          GetRenderPass()=VK_NULL_HANDLE, and zero log output.  The pipeline's
//          mode is decided at Initialize() time from the device probe; pre-init
//          the pipeline must look identical to its pre-E1 self.
// ---------------------------------------------------------------------------

TEST(E1_DynamicRendering, pipeline_default_construct_classic_codepath)
{
    VCK::Test::LogCapture cap;
    VCK::VulkanPipeline pipe;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
    ASSERT_FALSE(pipe.UsesDynamicRendering());
    ASSERT_TRUE(pipe.GetRenderPass() == VK_NULL_HANDLE);
    ASSERT_TRUE(pipe.GetDynamicColorFormat() == VK_FORMAT_UNDEFINED);
}


// ---------------------------------------------------------------------------
//  R19 — Shutdown on a default-constructed VulkanPipeline (which has never
//          been Initialize'd) is silent regardless of the rendering codepath
//          it would have selected at Initialize time.
// ---------------------------------------------------------------------------

TEST(E1_DynamicRendering, pipeline_shutdown_on_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    {
        VCK::VulkanPipeline pipe;
        pipe.Shutdown();
    }
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  R23 — cfg.rendering.mode is observable via GetRenderingMode() before
//          Initialize.  Captured at Initialize() time from cfg, but the default
//          constructor leaves it at the cfg default (Classic).  This pins the
//          behaviour so a future cfg change (e.g. flipping the default to
//          Dynamic) can't silently propagate.
// ---------------------------------------------------------------------------

TEST(E1_DynamicRendering, default_rendering_mode_is_classic_in_cfg)
{
    VCK::Config cfg;
    ASSERT_TRUE(cfg.rendering.mode == VCK::RenderingMode::Classic);
}


// ---------------------------------------------------------------------------
//  R19 — repeated Shutdown calls on a default-constructed VulkanPipeline are
//          silent (idempotent, no second-shutdown log).  Mirrors the contract
//          for VulkanDevice / VulkanCommand / VulkanSync.
// ---------------------------------------------------------------------------

TEST(E1_DynamicRendering, pipeline_double_shutdown_silent)
{
    VCK::Test::LogCapture cap;
    {
        VCK::VulkanPipeline pipe;
        pipe.Shutdown();
        pipe.Shutdown();
    }
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}
