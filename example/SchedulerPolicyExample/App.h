#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

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
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();
        void Shutdown();
        void Run();
    }
}
