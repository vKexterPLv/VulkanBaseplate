#pragma once
#include "VCK.h"
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
        void Init();
        void Shutdown();
        void Run();
    }
}
