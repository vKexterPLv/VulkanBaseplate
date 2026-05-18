#pragma once
#include "VCK.h"
// =============================================================================
//  DynamicRenderingExample
//
//  Demonstrates RenderingMode::Dynamic (VK_KHR_dynamic_rendering).
//  No VkRenderPass, no VkFramebuffer.  DrawFrame uses vkCmdBeginRendering /
//  vkCmdEndRendering with explicit synchronization2 image layout transitions.
//
//  cfg.rendering.mode = VCK::RenderingMode::Dynamic
//  VulkanFramebufferSet is NOT initialized.
// =============================================================================

namespace VCK {
    namespace DynamicRenderingExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
