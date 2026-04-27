#pragma once
#include "VCK.h"
#include <GLFW/glfw3.h>  // only for the OnKey callback signature
// =============================================================================
//  SchedulerPolicyExample
//
//  Same triangle scene as HelloExample, but press:
//
//    [1]  FramePolicy::Lockstep   - CPU blocks at EndFrame until GPU done
//    [2]  FramePolicy::Pipelined  - default, CPU N+1 / GPU N
//    [3]  FramePolicy::AsyncMax   - same as Pipelined here (lag naturally
//                                   capped by MAX_FRAMES_IN_FLIGHT=2)
//
//  The window title shows the current policy, and every 120 frames the
//  example logs the average CPU-frame microseconds per policy.
// =============================================================================

namespace VCK {
    namespace SchedulerPolicyExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
