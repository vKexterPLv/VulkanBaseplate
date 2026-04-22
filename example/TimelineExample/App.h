#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// =============================================================================
//  TimelineExample
//
//  Smoke test for TimelineSemaphore + DependencyToken.
//
//  On init, tries TimelineSemaphore::Initialize.  VulkanDevice does NOT
//  currently enable the timelineSemaphore feature bit, so Initialize will
//  log a fallback message on most setups.
//
//  When the feature IS available (future core change), the example runs a
//  host-signal / host-wait round-trip every 60 frames to prove the timeline
//  counter advances and DependencyToken::WaitHost unblocks correctly.
// =============================================================================

namespace VCK {
    namespace TimelineExample {
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();
        void Shutdown();
        void Run();
    }
}
