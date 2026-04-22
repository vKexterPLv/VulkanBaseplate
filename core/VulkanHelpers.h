#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake - do NOT redefine here.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
//  VCK logging
//
//  The logger writes to BOTH the Visual Studio Output window (OutputDebugStringA)
//  and the console (stdout).  Console output is ANSI-coloured; the debugger
//  pane gets the same text without escape codes so it stays readable.
//
//    VCKLog::Info ("Context",   "Instance created");
//    VCKLog::Warn ("Swapchain", "Mailbox unsupported, falling back to FIFO");
//    VCKLog::Error("Device",    "No suitable GPU");
//
//  Output:  [VCK] [Context] Instance created
//           ^cyan ^bold       ^default/yellow/red
//
//  The older one-arg LogVk("...") form is kept as a thin wrapper (tag = "VK")
//  so no existing call site has to change.
//
//  build.bat switches the console code page to 65001 (UTF-8) before running
//  g++, so the em-dash / box-drawing characters used elsewhere render cleanly.
// -----------------------------------------------------------------------------
namespace VCK { namespace Log {

enum class Level { Info, Warn, Error };

// ANSI colours - resolved at compile time so we pay no branch per log line.
constexpr const char* kReset    = "\x1b[0m";
constexpr const char* kBold     = "\x1b[1m";
constexpr const char* kDim      = "\x1b[2m";
constexpr const char* kCyan     = "\x1b[96m";
constexpr const char* kYellow   = "\x1b[93m";
constexpr const char* kRed      = "\x1b[91m";
constexpr const char* kWhite    = "\x1b[97m";

// Colour sequences are printf-free: OutputDebugStringA gets a stripped copy,
// stdout gets the coloured copy.  Hot paths are rare (one call per init step
// or per error), so the string assembly cost is irrelevant.
inline void Emit(Level level, const char* tag, const std::string& msg)
{
    const char* lvlColour =
        level == Level::Error ? kRed    :
        level == Level::Warn  ? kYellow :
                                kReset;

    // Coloured console line.
    std::string coloured;
    coloured.reserve(16 + msg.size());
    coloured += kCyan; coloured += "[VCK]"; coloured += kReset;
    coloured += ' ';
    coloured += kBold; coloured += kWhite; coloured += '['; coloured += tag; coloured += ']'; coloured += kReset;
    coloured += ' ';
    if (lvlColour != kReset) coloured += lvlColour;
    coloured += msg;
    if (lvlColour != kReset) coloured += kReset;
    coloured += '\n';

    std::fputs(coloured.c_str(), stdout);
    std::fflush(stdout);

    // Plain line for the debugger pane - no ANSI.
    std::string plain = "[VCK] [";
    plain += tag;
    plain += "] ";
    plain += msg;
    plain += '\n';
    OutputDebugStringA(plain.c_str());
}

inline void Info (const char* tag, const std::string& msg) { Emit(Level::Info,  tag, msg); }
inline void Warn (const char* tag, const std::string& msg) { Emit(Level::Warn,  tag, msg); }
inline void Error(const char* tag, const std::string& msg) { Emit(Level::Error, tag, msg); }

}} // namespace VCK::Log

// Short alias matching the public API in docs / wiki.
namespace VCKLog = VCK::Log;

// Backwards-compatible one-arg form.  New code should prefer
// VCKLog::Info("Swapchain", "...") which produces '[VCK] [Swapchain] ...'.
inline void LogVk(const std::string& message) {
    VCK::Log::Info("VK", message);
}

// Tagged overload - use this from new code.
inline void LogVk(const char* tag, const std::string& message) {
    VCK::Log::Info(tag, message);
}

// -----------------------------------------------------------------------------
//  VK_CHECK(expr)
//
//  Pass a single VkResult expression.  Stringifies the call for the error log.
//  Returns bool - true on VK_SUCCESS, false otherwise.
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
//  VCK::Config  -  master init-chain configuration
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
    Fifo,       // VK_PRESENT_MODE_FIFO_KHR       - always supported, vsync.
    Mailbox,    // VK_PRESENT_MODE_MAILBOX_KHR    - low-latency, tears nothing.
    Immediate,  // VK_PRESENT_MODE_IMMEDIATE_KHR  - may tear, uncapped.
};

enum class QueuePreference {
    GraphicsOnly,            // Just graphics + present.  Default.
    GraphicsCompute,         // Also pick a dedicated compute queue if one exists.
    GraphicsComputeTransfer, // Also pick a dedicated transfer queue if one exists.
};

// Compile-time upper bound on frames-in-flight.  Runtime `framesInFlight` is
// capped to this - deeper pipelining requires VK_KHR_timeline_semaphore.
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
        PresentMode                presentMode              = PresentMode::Mailbox;    // falls back to FIFO automatically
        uint32_t                   imageCount               = 0;                       // 0 = minImageCount + 1
        VkFormat                   surfaceFormat            = VK_FORMAT_UNDEFINED;     // UNDEFINED = auto-pick
        // RESERVED: MSAA is not yet wired end-to-end (no resolve attachment /
        // multisampled colour image).  Setting this > 1 logs a warning and is
        // clamped to VK_SAMPLE_COUNT_1_BIT in VulkanPipeline.  Field kept so
        // the API surface is stable when MSAA lands - see docs/Design.md.
        VkSampleCountFlagBits      msaaSamples              = VK_SAMPLE_COUNT_1_BIT;
        VkFormat                   depthFormat              = VK_FORMAT_UNDEFINED;     // UNDEFINED = auto
    } swapchain;

    struct SyncCfg
    {
        uint32_t                   framesInFlight           = 2; // clamped to MAX_FRAMES_IN_FLIGHT
    } sync;
};

} // namespace VCK
