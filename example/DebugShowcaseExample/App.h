#pragma once
#include "VCK.h"
// =============================================================================
//  DebugShowcaseExample
//
//  A guided tour of every debug-oriented feature in VCK:
//
//      * VCKLog - 4 levels (Info / Notice / Warn / Error)
//      * cfg.debug          - hides Info lines when false; opt-in for chatter
//      * dedup              - emits "(repeated N more times)" on spam
//      * VK_CHECK           - routes failure through VCKLog::Error (rule 14)
//      * Windows VT colour  - SetConsoleMode ENABLE_VIRTUAL_TERMINAL_PROCESSING
//      * Device property dump - GPU name, vendor, driver, limits
//      * Surface / swapchain caps - formats, present modes, max MSAA counts
//      * VCK_PLATFORM_* macros - which platform we are running on
//
//  There is no drawing loop.  A small 640 x 360 window exists just because
//  VulkanContext needs a surface to pick a physical device from.  The program
//  shuts down cleanly after the tour prints and the user closes the window.
// =============================================================================

namespace VCK { namespace DebugShowcaseExample {
    void Init();
    void Run();
    void Shutdown();
}}
