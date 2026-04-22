#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake — do NOT redefine here.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
//  LogVk — routes to BOTH the Visual Studio Output window
//          (OutputDebugStringA) and the console window (stdout / printf).
//  Global scope — usable from any file, any namespace.
//
//  Binaries built via example/build.bat use the default g++ console subsystem,
//  so stdout is already wired to whatever console launched the process — no
//  AllocConsole dance needed.
// -----------------------------------------------------------------------------
inline void LogVk(const std::string& message) {
    const std::string line = "[VK] " + message + "\n";
    OutputDebugStringA(line.c_str());
    std::fputs(line.c_str(), stdout);
    std::fflush(stdout);
}

// -----------------------------------------------------------------------------
//  VK_CHECK(expr)
//
//  Pass a single VkResult expression.  Stringifies the call for the error log.
//  Returns bool — true on VK_SUCCESS, false otherwise.
//
//  Usage:
//    if (!VK_CHECK(vkCreateFoo(...))) return false;
//    VK_CHECK(vkCreateFoo(...));           // fire-and-forget
// -----------------------------------------------------------------------------
#define VK_CHECK(expr)                                                          \
      ([&]() -> bool {                                                            \
          VkResult _vk_r = (expr);                                                \
          if (_vk_r != VK_SUCCESS) {                                              \
              LogVk(std::string("[FAILED] " #expr " -> ")                         \
                    + std::to_string(static_cast<int>(_vk_r)));                   \
              return false;                                                       \
          }                                                                       \
          return true;                                                            \
      }())


// -----------------------------------------------------------------------------
//  VCK::Config  —  master init-chain configuration
//
//  Every VulkanContext/Device/Swapchain/Sync/Command/Pipeline class accepts an
//  optional `const Config&` overload on its Initialize(...).  The old zero-arg
//  forms still work and map to a default-constructed Config, so existing code
//  keeps compiling unchanged.
//
//  Layout is grouped by the class that consumes each field, but passed as a
//  single object so you only build it once.
// -----------------------------------------------------------------------------
namespace VCK {

enum class PresentMode {
    Auto,       // Mailbox if supported, else FIFO.  Default.
    Fifo,       // VK_PRESENT_MODE_FIFO_KHR       — always supported, vsync.
    Mailbox,    // VK_PRESENT_MODE_MAILBOX_KHR    — low-latency, tears nothing.
    Immediate,  // VK_PRESENT_MODE_IMMEDIATE_KHR  — may tear, uncapped.
};

enum class QueuePreference {
    GraphicsOnly,            // Just graphics + present.  Default.
    GraphicsCompute,         // Also pick a dedicated compute queue if one exists.
    GraphicsComputeTransfer, // Also pick a dedicated transfer queue if one exists.
};

// Compile-time upper bound on frames-in-flight.  Runtime `framesInFlight` is
// capped to this — deeper pipelining requires VK_KHR_timeline_semaphore.
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

struct Config
{
    struct ContextCfg
    {
        std::string                appName                  = "VCK App";
        bool                       enableValidation         = true;
        std::vector<const char*>   extraInstanceLayers;
        std::vector<const char*>   extraInstanceExtensions;
    } context;

    struct DeviceCfg
    {
        bool                       preferDiscreteGpu        = true;
        std::vector<const char*>   extraDeviceExtensions;
        QueuePreference            queuePreference          = QueuePreference::GraphicsOnly;
    } device;

    struct SwapchainCfg
    {
        PresentMode                presentMode              = PresentMode::Auto;
        uint32_t                   imageCount               = 0;                       // 0 = minImageCount + 1
        VkFormat                   surfaceFormat            = VK_FORMAT_UNDEFINED;     // UNDEFINED = auto-pick
        // RESERVED: MSAA is not yet wired end-to-end (no resolve attachment /
        // multisampled colour image).  Setting this > 1 logs a warning and is
        // clamped to VK_SAMPLE_COUNT_1_BIT in VulkanPipeline.  Field kept so
        // the API surface is stable when MSAA lands — see docs/Design.md.
        VkSampleCountFlagBits      msaaSamples              = VK_SAMPLE_COUNT_1_BIT;
        VkFormat                   depthFormat              = VK_FORMAT_UNDEFINED;     // UNDEFINED = auto
    } swapchain;

    struct SyncCfg
    {
        uint32_t                   framesInFlight           = 2; // clamped to MAX_FRAMES_IN_FLIGHT
    } sync;
};

} // namespace VCK
