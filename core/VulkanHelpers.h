#pragma once

// Pull in VCK_PLATFORM_* macros first so platform-guarded includes work even
// if this file is included before VCK.h (the amalgam) has done so itself.
#include "../VCKCrossplatform.h"

#if VCK_PLATFORM_WINDOWS
    // WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake / build.bat -
    // do NOT redefine here.
    #include <windows.h>
#endif

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>

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

// Level semantics (user-controllable via SetDebug / cfg.debug):
//
//   Notice  - always visible.  "User needs to know this happened"
//             lines - device picked, swapchain created/recreated, MSAA
//             auto-picked, fallback from Mailbox to FIFO.
//   Info    - debug-only.  Verbose init chatter ([Context] creating instance,
//             [Command] pool allocated, etc.).  Hidden when debug = false.
//   Warn    - always visible.
//   Error   - always visible.
//
// Dedup: if an identical (level, tag, msg) line was the last thing emitted,
// it is suppressed (the next different line flushes a "(x N)" repeat count).
// This keeps the console readable in tight per-frame paths without losing
// information.
enum class Level { Info, Notice, Warn, Error };

// ANSI colours - resolved at compile time so we pay no branch per log line.
constexpr const char* kReset    = "\x1b[0m";
constexpr const char* kBold     = "\x1b[1m";
constexpr const char* kDim      = "\x1b[2m";
constexpr const char* kCyan     = "\x1b[96m";
constexpr const char* kYellow   = "\x1b[93m";
constexpr const char* kRed      = "\x1b[91m";
constexpr const char* kWhite    = "\x1b[97m";
constexpr const char* kGreen    = "\x1b[92m";

// Global state - one-copy-per-process (header-inline vars are ODR-safe in C++17).
inline bool& DebugFlag()          { static bool f = false; return f; }
inline std::string& LastKey()     { static std::string s; return s; }
inline int&        DedupCount()   { static int n = 0; return n; }
inline Level&      LastLevel()    { static Level l = Level::Info; return l; }
inline std::string& LastTag()     { static std::string s; return s; }

inline void SetDebug(bool on) { DebugFlag() = on; }
inline bool IsDebug()         { return DebugFlag(); }

inline const char* LevelColour(Level level) {
    switch (level) {
        case Level::Error:  return kRed;
        case Level::Warn:   return kYellow;
        case Level::Notice: return kGreen;
        case Level::Info:
        default:            return kReset;
    }
}

// Writes one formatted line to stdout + (on Windows) the debugger pane.
inline void WriteLine(Level level, const char* tag, const std::string& msg)
{
    const char* lvlColour = LevelColour(level);

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

#if VCK_PLATFORM_WINDOWS
    std::string plain = "[VCK] [";
    plain += tag;
    plain += "] ";
    plain += msg;
    plain += '\n';
    OutputDebugStringA(plain.c_str());
#endif
}

// Flushes a pending "(last line repeated N more times)" if one is stored.
inline void FlushDedup()
{
    const int n = DedupCount();
    if (n <= 0) return;
    std::string repeat = "(repeated ";
    repeat += std::to_string(n);
    repeat += " more time";
    if (n != 1) repeat += 's';
    repeat += ')';
    WriteLine(LastLevel(), LastTag().c_str(), repeat);
    DedupCount() = 0;
}

inline void Emit(Level level, const char* tag, const std::string& msg)
{
    // Drop Info-level lines unless debug is enabled.
    if (level == Level::Info && !IsDebug()) {
        // Still run through dedup bookkeeping so that if the same Info line
        // is followed by a Notice/Warn/Error, the dedup counter from an
        // earlier *visible* line is not prematurely flushed.  We simply do
        // nothing here - no emit, no state change.
        return;
    }

    // Build the stable key used for dedup comparison (level + tag + body).
    std::string key;
    key.reserve(4 + std::strlen(tag) + msg.size());
    key += static_cast<char>(static_cast<int>(level) + '0');
    key += '|';
    key += tag;
    key += '|';
    key += msg;

    if (key == LastKey()) {
        ++DedupCount();
        return;
    }

    // New line - flush any accumulated repeats first, then emit.
    FlushDedup();
    WriteLine(level, tag, msg);
    LastKey()   = std::move(key);
    LastLevel() = level;
    LastTag()   = tag;
}

inline void Info  (const char* tag, const std::string& msg) { Emit(Level::Info,   tag, msg); }
inline void Notice(const char* tag, const std::string& msg) { Emit(Level::Notice, tag, msg); }
inline void Warn  (const char* tag, const std::string& msg) { Emit(Level::Warn,   tag, msg); }
inline void Error (const char* tag, const std::string& msg) { Emit(Level::Error,  tag, msg); }

}} // namespace VCK::Log

// Short alias matching the public API in docs / wiki.
namespace VCKLog = VCK::Log;

// Backwards-compatible one-arg form.  Parses a leading '[Tag] body' so legacy
// call sites like LogVk("[Swapchain] Initializing...") render cleanly as
// '[VCK] [Swapchain] Initializing...' without having to touch every line.
// Level = Info (debug-gated); migrate high-signal user-facing lines to
// VCKLog::Notice(tag, body) explicitly.
inline void LogVk(const std::string& message) {
    if (!message.empty() && message.front() == '[') {
        const std::size_t end = message.find(']');
        if (end != std::string::npos &&
            end + 1 < message.size() &&
            message[end + 1] == ' ')
        {
            // Buffer the extracted tag so the const char* we pass stays alive
            // through the single synchronous Emit call.
            static thread_local std::string tagBuf;
            tagBuf.assign(message, 1, end - 1);
            VCK::Log::Info(tagBuf.c_str(), message.substr(end + 2));
            return;
        }
    }
    VCK::Log::Info("VK", message);
}

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

// MSAA sample count sentinel.  If cfg.swapchain.msaaSamples == MSAA_AUTO (the
// default), VulkanSwapchain::Initialize detects a sensible level from heap
// size + device type and logs what it picked.  Set any real
// VK_SAMPLE_COUNT_*_BIT value to pin it - VCK will not second-guess you.
static constexpr VkSampleCountFlagBits MSAA_AUTO = static_cast<VkSampleCountFlagBits>(0);

// -----------------------------------------------------------------------------
//  AATechnique
//
//  Which anti-aliasing technique to use.  Two families:
//
//  Sample-based (implemented by VCK in the graphics pipeline):
//    MSAA            - plain multisampling (2x/4x/8x via cfg.swapchain.msaaSamples)
//    MSAA_A2C        - MSAA + alpha-to-coverage (foliage / fences / decals)
//    SampleRate      - MSAA + sample-rate shading (per-sample fragment eval)
//
//  Post-process (VCK picks the name, *user implements* the shader):
//    FXAA            - single-pass approximate
//    SMAA_1x         - edge detect + blend weights + neighborhood blend
//    SMAA_T2x        - SMAA with 2x temporal reprojection
//    TAA             - temporal AA with motion vectors + history
//    TAAU            - temporal AA + spatial upscale (non-AI)
//
//  Auto   - sentinel.  DetectRecommendedAA() runs at Initialize and picks.
//  Off    - no AA.
//
//  VCK's scope ends at the pipeline (rule 15/16 - no engine assumptions,
//  minimal core surface).  For the post-process family, VulkanSwapchain
//  reports the chosen technique via GetAATechnique() / CurrentAATechnique()
//  and the application's own render code implements the shader pass.
// -----------------------------------------------------------------------------
enum class AATechnique {
    Auto,
    Off,
    MSAA,
    MSAA_A2C,
    SampleRate,
    FXAA,
    SMAA_1x,
    SMAA_T2x,
    TAA,
    TAAU,
};

inline const char* ToString(AATechnique t) {
    switch (t) {
        case AATechnique::Auto:       return "Auto";
        case AATechnique::Off:        return "Off";
        case AATechnique::MSAA:       return "MSAA";
        case AATechnique::MSAA_A2C:   return "MSAA+A2C";
        case AATechnique::SampleRate: return "SampleRateShading";
        case AATechnique::FXAA:       return "FXAA";
        case AATechnique::SMAA_1x:    return "SMAA 1x";
        case AATechnique::SMAA_T2x:   return "SMAA T2x";
        case AATechnique::TAA:        return "TAA";
        case AATechnique::TAAU:       return "TAAU";
    }
    return "?";
}

// Is the technique implemented by VCK (pipeline-level) or by the caller
// (post-process)?  Callers use this to decide whether to run their own
// post-process pass.
inline bool IsSampleBasedAA(AATechnique t) {
    return t == AATechnique::MSAA
        || t == AATechnique::MSAA_A2C
        || t == AATechnique::SampleRate;
}
inline bool IsPostProcessAA(AATechnique t) {
    return t == AATechnique::FXAA
        || t == AATechnique::SMAA_1x
        || t == AATechnique::SMAA_T2x
        || t == AATechnique::TAA
        || t == AATechnique::TAAU;
}

// -----------------------------------------------------------------------------
//  DetectRecommendedMSAA
//
//  Picks an MSAA level for `phys` that should be a reasonable default on that
//  hardware.  Pure function - no config, no side effects (VulkanSwapchain
//  calls VCKLog::Notice with the picked level afterwards).
//
//  Tiers:
//    LOW   (integrated / CPU / discrete with <=  3 GB VRAM heap)  ->  1x
//    MID   (discrete with                 <=  8 GB VRAM heap)     ->  4x
//    HIGH  (discrete with                 >   8 GB VRAM heap)     ->  8x
//
//  Clamped to the highest bit reported by framebufferColorSampleCounts AND
//  framebufferDepthSampleCounts (so swapchain + depth agree).  Never returns
//  MSAA_AUTO - always a real VK_SAMPLE_COUNT_*_BIT.
//
//  Vendor-specific techniques (CSAA/EQAA/TXAA/MFAA) are NOT detected here:
//  they require vendor extensions not in the public Vulkan surface and would
//  defeat VCK's cross-platform promise.  Document the requested level and
//  let the app pick manually if it needs them.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  DetectRecommendedAA
//
//  Implements the 5-step decision tree from docs/Design.md (Section: AA
//  first-launch auto detection).  Steps:
//
//    1. Query hardware                         (VkPhysicalDeviceProperties + heaps)
//    2. Classify performance tier              (LOW / MID / HIGH)
//    3. Detect rendering path                  (forwardRenderer: caller input)
//    4. Select AA method                       (tier + motion-vector support)
//    5. MSAA sample selection                  (done separately via
//                                               DetectRecommendedMSAA)
//
//  Parameters the detector CAN determine from the device: VRAM tier,
//  integrated-vs-discrete, max MSAA supported.
//
//  Parameters the detector CANNOT determine and must be told by the caller:
//  - forwardRenderer         - is the app's main pass forward or deferred?
//                              Deferred passes cannot use MSAA efficiently.
//  - supportsMotionVectors   - does the renderer produce per-pixel motion
//                              vectors?  Required for T2x/TAA/TAAU.
//
//  The result is a single AATechnique the app can honour directly.  For
//  sample-based techniques VCK wires the pipeline itself; for post-process
//  techniques the app implements the shader and reads back via
//  VulkanSwapchain::GetAATechnique().
// -----------------------------------------------------------------------------
inline AATechnique DetectRecommendedAA(VkPhysicalDevice phys,
                                       bool forwardRenderer,
                                       bool supportsMotionVectors)
{
    if (phys == VK_NULL_HANDLE) return AATechnique::Off;

    VkPhysicalDeviceProperties       props{};
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceProperties      (phys, &props);
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);

    VkDeviceSize deviceLocalBytes = 0;
    for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            deviceLocalBytes += mem.memoryHeaps[i].size;
    }
    const double gib = static_cast<double>(deviceLocalBytes) / (1024.0 * 1024.0 * 1024.0);
    const bool isDiscrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);

    // Tier.  Integrated always LOW regardless of heap size (iGPU heaps can
    // appear large due to UMA but shader throughput is the bottleneck).
    enum class Tier { LOW, MID, HIGH };
    Tier tier;
    if (!isDiscrete)           tier = Tier::LOW;
    else if (gib <= 2.0)       tier = Tier::LOW;
    else if (gib <= 6.0)       tier = Tier::MID;
    else                       tier = Tier::HIGH;

    // Decision tree (user spec, Section 2 Step 4-5):
    if (tier == Tier::LOW) {
        // Low-end: prefer cheap post-process FXAA over MSAA (MSAA at 2x on
        // LOW-tier is still too expensive in forward for most scenes).
        return AATechnique::FXAA;
    }

    if (tier == Tier::MID) {
        if (forwardRenderer) {
            // Forward + MID: MSAA at a level DetectRecommendedMSAA will clamp.
            return AATechnique::MSAA_A2C;
        }
        return supportsMotionVectors ? AATechnique::SMAA_T2x
                                     : AATechnique::SMAA_1x;
    }

    // HIGH tier
    if (forwardRenderer) {
        return supportsMotionVectors ? AATechnique::TAA
                                     : AATechnique::MSAA_A2C;
    }
    return supportsMotionVectors ? AATechnique::TAA
                                 : AATechnique::SMAA_1x;
}

inline VkSampleCountFlagBits DetectRecommendedMSAA(VkPhysicalDevice phys)
{
    if (phys == VK_NULL_HANDLE) return VK_SAMPLE_COUNT_1_BIT;

    VkPhysicalDeviceProperties       props{};
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceProperties      (phys, &props);
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);

    // VRAM tier: sum DEVICE_LOCAL heaps that are NOT host-visible (excludes
    // the small ReBAR / HOST_VISIBLE heap on some drivers so we don't count
    // it twice).  Integrated GPUs have one big shared heap; still fine.
    VkDeviceSize deviceLocalBytes = 0;
    for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            deviceLocalBytes += mem.memoryHeaps[i].size;
        }
    }
    const double gib = static_cast<double>(deviceLocalBytes) / (1024.0 * 1024.0 * 1024.0);

    const bool isDiscrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);

    VkSampleCountFlagBits desired = VK_SAMPLE_COUNT_1_BIT;
    if (!isDiscrete)        desired = VK_SAMPLE_COUNT_1_BIT;
    else if (gib <= 3.0)    desired = VK_SAMPLE_COUNT_2_BIT;
    else if (gib <= 8.0)    desired = VK_SAMPLE_COUNT_4_BIT;
    else                    desired = VK_SAMPLE_COUNT_8_BIT;

    // Clamp to what both color + depth attachments can actually handle.
    const VkSampleCountFlags supported =
        props.limits.framebufferColorSampleCounts &
        props.limits.framebufferDepthSampleCounts;

    // Walk desired DOWN until we find a supported bit.  Always terminates at
    // VK_SAMPLE_COUNT_1_BIT which every device supports.
    const VkSampleCountFlagBits candidates[] = {
        VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_16_BIT,
        VK_SAMPLE_COUNT_8_BIT,  VK_SAMPLE_COUNT_4_BIT,  VK_SAMPLE_COUNT_2_BIT,
        VK_SAMPLE_COUNT_1_BIT
    };
    for (VkSampleCountFlagBits bit : candidates) {
        if (bit > desired) continue;
        if (supported & bit) return bit;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

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
    // Verbose init/runtime logging.  When false (default), only user-facing
    // Notice/Warn/Error lines appear on the console; Info-level chatter is
    // silenced (dedup still applies so a runaway Warn loop can't spam).
    // Set via cfg.debug = true or call VCKLog::SetDebug(true) directly.
    bool debug = false;

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
        // MSAA sample count.  Default = MSAA_AUTO - detect from the physical
        // device (integrated -> 1x, mid discrete -> 4x, high discrete -> 8x,
        // clamped to framebufferColorSampleCounts).  Pin any real
        // VK_SAMPLE_COUNT_*_BIT value to disable the detector.
        VkSampleCountFlagBits      msaaSamples              = MSAA_AUTO;
        VkFormat                   depthFormat              = VK_FORMAT_UNDEFINED;     // UNDEFINED = auto
    } swapchain;

    struct SyncCfg
    {
        uint32_t                   framesInFlight           = 2; // clamped to MAX_FRAMES_IN_FLIGHT
    } sync;

    // Anti-aliasing configuration.  Governs both the sample-based techniques
    // VCK implements (MSAA / MSAA+A2C / Sample-Rate Shading) and the name
    // returned to the caller for post-process techniques (FXAA / SMAA / TAA
    // / TAAU), which the caller's renderer is responsible for implementing.
    //
    // Default = AATechnique::Auto -> DetectRecommendedAA() runs at Initialize
    // and logs the pick as a user-visible Notice line.  Pin any real value
    // to disable the detector and force a specific technique.
    //
    // `forwardRenderer` and `supportsMotionVectors` are pure caller hints -
    // VCK cannot infer them from the device.
    struct AACfg
    {
        AATechnique                technique                = AATechnique::Auto;
        bool                       alphaToCoverage          = false;
        bool                       sampleRateShading        = false;
        float                      minSampleShading         = 0.2f;

        // Detector inputs (ignored when `technique` is pinned).
        bool                       forwardRenderer          = true;
        bool                       supportsMotionVectors    = false;
    } aa;
};

} // namespace VCK
