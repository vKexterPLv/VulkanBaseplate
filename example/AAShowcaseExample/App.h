#pragma once
#include "VCK.h"
// =============================================================================
//  AAShowcaseExample
//
//  Proof that VCK's cfg.aa block is not just a toggle:
//
//      * AATechnique enum     - 10 named options (Auto + 4 sample-based +
//                               5 post-process names).
//      * DetectRecommendedAA  - the 5-step decision tree from docs/Design.md
//                               runs once at Initialize when technique = Auto.
//      * VulkanSwapchain getters - GetAATechnique() / GetMSAASamples() expose
//                                  exactly what the detector picked so the
//                                  app can display it or log it.
//
//  At launch this example:
//      1. queries the GPU (vendor / tier / max-MSAA),
//      2. prints what DetectRecommendedAA returns for the 4 combinations of
//         forwardRenderer={true,false} x motionVectors={true,false},
//      3. creates a swapchain with AATechnique::Auto and prints what got
//         picked + the clamped MSAA sample count,
//      4. renders a triangle so sample-based AA is visually visible.
// =============================================================================

namespace VCK { namespace AAShowcaseExample {
    void Init();
    void Run();
    void Shutdown();
}}
