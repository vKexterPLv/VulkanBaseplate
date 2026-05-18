#pragma once
#include "VCK.h"
// =============================================================================
//  BindlessExample
//
//  Demonstrates cfg.device.enableBindless + VulkanDescriptorAllocator::
//  InitializeBindless / WriteBindless.
//
//  Creates a small bindless COMBINED_IMAGE_SAMPLER array (4 slots), uploads
//  four 1×1 solid-colour textures, and draws a quad selecting the texture
//  index via gl_VertexIndex.
// =============================================================================

namespace VCK {
    namespace BindlessExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
