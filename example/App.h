#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace GTA_Sandbox {
    namespace App {
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();        // Initialise GLFW, window, all Vulkan objects
        void Shutdown();    // Destroy all Vulkan objects, destroy GLFW window
        void Run();         // Called by main.cpp — Init → loop → Shutdown
    }
}
