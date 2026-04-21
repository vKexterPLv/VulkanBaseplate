#pragma once
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// =============================================================================
//  VMMExample
//
//  Demonstrates all three layers of VulkanMemoryManager in a live render loop.
//
//  What runs:
//    • A static RGB triangle uploaded once via the VMM staging ring and kept
//      in a Persistent GPU-side vertex buffer.  (PersistentPool + StagingRing)
//
//    • A dynamic quad whose vertex positions are rebuilt every frame using
//      the TransientPool — the vertices animate by bouncing the quad corners.
//      No per-frame vmaCreateBuffer calls, zero allocation overhead.
//
//    • A texture allocated through AllocPersistentImage and uploaded via
//      StageToImage — shows the image path of the staging ring.
//
//    • LogStats() is called every 120 frames to show the registry state
//      in the VS Output window.
//
//  Demonstrates:
//    Layer 1 — VmmRawAlloc (used internally; VmmBuffer / VmmImage structs visible)
//    Layer 2 — Lifetime tagging (Persistent vs TransientFrame), registry stats
//    Layer 3 — vmm.AllocPersistent, vmm.AllocTransient, vmm.StageToBuffer,
//               vmm.StageToImage, vmm.BeginFrame, vmm.EndFrame, vmm.FlushStaging
// =============================================================================

namespace VulkanBaseplate {
    namespace VMMExample {
        extern GLFWwindow* window;
        extern int         window_width;
        extern int         window_height;

        void Init();
        void Shutdown();
        void Run();
    }
}
