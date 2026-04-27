#pragma once
#include "VCK.h"
// =============================================================================
//  SecondaryCmdExample  (v0.3)
//
//  Demonstrates VulkanCommand's secondary command buffer API:
//
//      VkCommandBuffer cb = command.AllocateSecondary();
//      command.BeginSecondary(cb, inheritance);
//      // ... record draws ...
//      command.EndSecondary(cb);
//
//      vkCmdBeginRenderPass(primary, ..., VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
//      VulkanCommand::ExecuteSecondaries(primary, &cb, 1);
//      vkCmdEndRenderPass(primary);
//
//  Draws the same RGB triangle as the RGBTriangle example, but the draw
//  commands live in a secondary command buffer re-recorded each frame.
//  Shows the pool / inheritance / contents-flag plumbing in the most
//  minimal form so it can be copy-pasted into an engine codebase.
//
//  Rule 20 (example parity): every public class in VCK.h is exercised
//  by at least one example - this one covers the secondary-command
//  helpers that shipped in v0.3.
// =============================================================================

namespace VCK {
    namespace SecondaryCmdExample {
        void Init();
        void Shutdown();
        void Run();
    }
}
