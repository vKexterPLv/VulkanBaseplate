#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// =============================================================================
//  DebugTimelineExample
//
//  Exercises DebugTimeline's CPU span + stall recording and Dump().
//
//  Per frame:
//    - scheduler records "frame" and "jobs" spans automatically.
//    - this example adds "simulate" and "record" spans around the user code.
//    - every 120 frames, it calls scheduler.Timeline().Dump() which prints
//      a chronological list of spans and stalls via LogVk.
// =============================================================================

namespace VCK {
    namespace DebugTimelineExample {
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();
        void Shutdown();
        void Run();
    }
}
