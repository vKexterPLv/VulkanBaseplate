#pragma once
#include "VCK.h"
// =============================================================================
//  SubmissionBatchingExample
//
//  Demonstrates that multiple command buffers queued into the same frame
//  result in a single vkQueueSubmit.
//
//  Per frame:
//    - "pre" primary cmd  (allocated from an extra command pool) - runs
//                          setup work (e.g. image layout probe); batched
//                          without wait/signal semas.
//    - "main" primary cmd (from scheduler/VulkanCommand)         - renders
//                          the triangle; waits on ImageAvailable, signals
//                          RenderFinished.
//
//  Both are queued via Frame::Submissions().QueueGraphics(...); EndFrame
//  flushes them with one vkQueueSubmit submit-info array.
// =============================================================================

namespace VCK {
    namespace SubmissionBatchingExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
