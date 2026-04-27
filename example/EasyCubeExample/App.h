#pragma once
#include "VCK.h"
// =============================================================================
//  EasyCubeExample  -  showcase of the v0.3 ergonomic shader API.
//
//  Renders a single rotating cube.  The interesting part is NOT the cube;
//  it's how little the example has to write to get the cube on screen.
//  Everything you used to hand-pack (vertex tables, push-constant byte
//  offsets, matrix math) is now one named VCK call.
//
//  Demonstrates:
//    Math        - VCK::Vec3 / Mat4 / Perspective / LookAt / Rotate
//    Expansion   - VCK::VertexLayout (named vertex attributes)
//                - VCK::PushConstants (named push-constant slots)
//                - VCK::Primitives::Cube() (CPU-side mesh in one call)
//                - VCK::VulkanMesh (GPU upload + draw)
//    Execution   - VCK::FrameScheduler (Pipelined policy)
//
//  Every frame:
//
//      VCK::Mat4 mvp = proj * view * VCK::Rotate({0,1,0}, angle);
//      pc.Set("mvp", mvp);
//
//      Frame& f = scheduler.BeginFrame();
//        (acquire + begin render pass)
//        pc.Apply(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT);
//        mesh.RecordDraw(cb);
//        (end render pass + submit)
//      scheduler.EndFrame();
//
//  Compare with a pre-v0.3 cube: 40 lines of positions[], 48 lines of push
//  constant struct + offset math, 20 lines of matrix helpers.  Here: 0.
// =============================================================================

namespace VCK {
    namespace EasyCubeExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
