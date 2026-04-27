#pragma once
#include "VCK.h"
// =============================================================================
//  ShaderToolingExample  -  exercises the v0.4 shader tooling layer.
//
//  Renders one triangle.  The interesting part is what's NOT in this file:
//  no hand-written `LoadSpv`, no manually-built `VkSpecializationInfo`, no
//  loose `VertexLayout` / `PushConstants` declarations sprinkled around the
//  pipeline call.  Everything the shader needs is declared once in a
//  `ShaderInterface` and pulled back out by the pipeline.
//
//  Demonstrates (R20: every public class has at least one example):
//    [26] VCK::ShaderLoader     - LoadFromFile per stage; GetShaderInfo()
//    [27] VCK::ShaderWatcher    - per-frame `HasChanged` poll; debug-gated
//    [28] VCK::SpecConstants    - one float spec constant for the brightness
//    [29] VCK::ShaderStage      - per-stage VertexLayout + PushConstants
//    [30] VCK::ShaderInterface  - merges stages into VertexInput + Config
//                                 also builds the empty layout the pipeline
//                                 doesn't need (still demonstrates the call)
//
//  Hot reload: when `cfg.debug == true` the example holds a ShaderWatcher
//  on both `.spv` files and rebuilds the pipeline whenever either changes.
//  Touch `assets/shadertool.frag.spv` while running and the brightness will
//  flip between the spec-constant value and whatever the new shader emits.
//
//  Every frame:
//
//      VCK::Mat4 mvp = proj * view * VCK::Rotate({0,1,0}, angle);
//      pc.Set("mvp", mvp);
//
//      Frame& f = scheduler.BeginFrame();
//        if (cfg.debug && watcher.HasChanged()) ReinitPipeline();
//        (acquire + begin render pass)
//        pc.Apply(cb, layout, VK_SHADER_STAGE_VERTEX_BIT);
//        mesh.RecordDraw(cb);
//        (end render pass + submit)
//      scheduler.EndFrame();
// =============================================================================

namespace VCK {
    namespace ShaderToolingExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
