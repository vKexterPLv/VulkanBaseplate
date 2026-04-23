#pragma once
#include "VCK.h"
// =============================================================================
//  JobGraphExample
//
//  Shows how to schedule CPU work per frame through Frame::Jobs() before
//  recording GPU commands.  Four jobs with dependencies:
//
//     [physics] ──┐
//                 ├──> [build-cmds]
//     [animate] ──┘
//     [audio]
//
//  physics and animate run in parallel (no deps between them); build-cmds
//  waits on both; audio is a standalone job with no deps.  Every 60 frames
//  the example logs the measured durations.
// =============================================================================

namespace VCK {
    namespace JobGraphExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
