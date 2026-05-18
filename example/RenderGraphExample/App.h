#pragma once
#include "VCK.h"
// =============================================================================
//  RenderGraphExample
//
//  Demonstrates RenderGraph: two-pass declarative graph.
//    Pass 0 "offscreen" — writes to a managed colour target.
//    Pass 1 "present"   — reads the colour target, writes to swapchain image.
//  RenderGraph::Compile() topologically sorts and inserts vkCmdPipelineBarrier2.
//  RenderGraph::Execute() runs the passes in order.
// =============================================================================

namespace VCK {
    namespace RenderGraphExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
