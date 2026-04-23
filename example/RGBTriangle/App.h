#pragma once
#include "VCK.h"
// =============================================================================
//  RGBTriangle
//
//  Minimal example: a vertex-coloured triangle with live window resize.
//
//  Demonstrates:
//    VulkanContext, VulkanDevice, VulkanSwapchain, VulkanPipeline,
//    VulkanCommand, VulkanSync, VulkanFramebufferSet, VulkanMesh
//
//  No UBOs, no descriptors, no textures, no push constants.
//  The pipeline uses no descriptor set layouts - the vertex shader reads
//  position and color directly from the vertex buffer.
// =============================================================================

namespace VCK {
    namespace RGBTriangle {
        void Init();
        void Shutdown();
        void Run();
    }
}
