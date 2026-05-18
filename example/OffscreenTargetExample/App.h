#pragma once
#include "VCK.h"
// =============================================================================
//  OffscreenTargetExample
//
//  Demonstrates VCK::OffscreenTarget + VCK::FullscreenPass.
//  Renders a solid-colour triangle to an OffscreenTarget, then blits the
//  result to the swapchain via a FullscreenPass.
// =============================================================================

namespace VCK {
    namespace OffscreenTargetExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
