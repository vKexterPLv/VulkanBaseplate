#pragma once
#include "VCK.h"
// =============================================================================
//  HelloExample
//
//  The smallest possible program that drives the full VCKExpansion
//  execution layer.  One coloured triangle, one FrameScheduler,
//  FramePolicy::Pipelined.
//
//  Demonstrates:
//    Core        - VulkanContext, VulkanDevice, VulkanSwapchain,
//                  VulkanPipeline, VulkanCommand, VulkanSync
//    Expansion   - VulkanFramebufferSet, VulkanMesh
//    Execution   - FrameScheduler, Frame, GpuSubmissionBatcher
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
        void Init();
        void Shutdown();
        void Run();
    }
}
