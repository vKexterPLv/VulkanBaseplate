#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

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
//  The pipeline uses no descriptor set layouts — the vertex shader reads
//  position and color directly from the vertex buffer.
// =============================================================================

namespace VCK {
    namespace RGBTriangle {
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();
        void Shutdown();
        void Run();
    }
}
