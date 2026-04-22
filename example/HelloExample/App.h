#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// =============================================================================
//  HelloExample
//
//  The smallest possible program that drives the full VCKExpansion
//  execution layer.  One coloured triangle, one FrameScheduler,
//  FramePolicy::Pipelined.
//
//  Demonstrates:
//    Core        — VulkanContext, VulkanDevice, VulkanSwapchain,
//                  VulkanPipeline, VulkanCommand, VulkanSync
//    Expansion   — VulkanFramebufferSet, VulkanMesh
//    Execution   — FrameScheduler, Frame, GpuSubmissionBatcher
//
//  Every frame:
//
//      Frame& f = scheduler.BeginFrame();
//          (acquire next swapchain image)
//          (record render pass into f.PrimaryCmd())
//          f.Submissions().QueueGraphics(f.PrimaryCmd(), submitInfo);
//      scheduler.EndFrame();      // flushes the batched submit
//      (vkQueuePresentKHR ...)
// =============================================================================

namespace VCK {
    namespace HelloExample {
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();
        void Shutdown();
        void Run();
    }
}
