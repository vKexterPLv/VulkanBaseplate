#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// =============================================================================
//  MipmapExample
//
//  Demonstrates VulkanMipmapGenerator end-to-end.
//
//  A 256x256 procedural checkerboard is uploaded to a mip-aware VkImage
//  (created manually with mipLevels = MipLevels(256, 256) = 9 and
//  TRANSFER_SRC | TRANSFER_DST | SAMPLED usage).  VulkanMipmapGenerator
//  then generates all 9 levels via blit.  The result is bound as a combined
//  image/sampler and drawn onto a full-screen quad.
//
//  At normal zoom the highest mip is visible.  Resize the window very small
//  to see the GPU switch to coarser mip levels (checkerboard becomes grey).
// =============================================================================

namespace VCK {
    namespace MipmapExample {
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();
        void Shutdown();
        void Run();
    }
}
