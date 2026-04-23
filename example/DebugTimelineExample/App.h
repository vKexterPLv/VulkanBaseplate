#pragma once
#include "VCK.h"
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
        void Init();
        void Shutdown();
        void Run();
    }
}
