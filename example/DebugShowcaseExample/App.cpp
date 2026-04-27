#include "App.h"
#include "VCK.h"
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
//  DebugShowcaseExample
//
//  Everything VCK exposes for debugging, demonstrated in one place.  No draw
//  loop - just a tour that prints to stdout / debugger pane.
//
//  Sections, in order:
//      [1] Platform + build info
//      [2] VCKLog levels and colours
//      [3] cfg.debug gating of Info lines
//      [4] Dedup demo
//      [5] VK_CHECK fail-loud path
//      [6] Device / driver / vendor dump
//      [7] Surface + swapchain capabilities dump
//      [8] Memory heap dump
//      [9] How to hook DebugTimeline on a real frame loop (pointer to
//          DebugTimelineExample)
//
//  Close the window (or press ESC via the window manager) to exit.
// =============================================================================

namespace VCK::DebugShowcaseExample {

    // -------------------------------------------------------------------------
    //  State
    // -------------------------------------------------------------------------
    std::string   title = "DebugShowcaseExample - close the window to exit";
    VCK::Window   window;
    VulkanContext context;
    VulkanDevice  device;

    static void Sleep(int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    static std::string ByteUnits(VkDeviceSize bytes) {
        constexpr VkDeviceSize GB = 1ull << 30;
        constexpr VkDeviceSize MB = 1ull << 20;
        std::ostringstream os;
        os.precision(2);
        os << std::fixed;
        if (bytes >= GB) os << (static_cast<double>(bytes) / static_cast<double>(GB)) << " GB";
        else             os << (static_cast<double>(bytes) / static_cast<double>(MB)) << " MB";
        return os.str();
    }

    // -------------------------------------------------------------------------
    //  [1] Platform + build info
    // -------------------------------------------------------------------------
    static void PrintPlatform()
    {
        VCKLog::Notice("Platform", "--- Section 1: VCK_PLATFORM_* ---");
#if VCK_PLATFORM_WINDOWS
        VCKLog::Info  ("Platform", "Windows build (VCK_PLATFORM_WINDOWS=1)");
#elif VCK_PLATFORM_LINUX
        VCKLog::Info  ("Platform", "Linux build (VCK_PLATFORM_LINUX=1)");
#elif VCK_PLATFORM_MACOS
        VCKLog::Info  ("Platform", "macOS build (VCK_PLATFORM_MACOS=1)");
#else
        VCKLog::Warn  ("Platform", "Unknown platform - VCK_PLATFORM_* is 0");
#endif
        VCKLog::Info  ("Platform", std::string("C++ standard: ") + std::to_string(__cplusplus));
#if defined(__GNUC__) && !defined(__clang__)
        VCKLog::Info  ("Platform", "Compiler: GCC " + std::string(__VERSION__));
#elif defined(__clang__)
        VCKLog::Info  ("Platform", "Compiler: clang " + std::string(__clang_version__));
#elif defined(_MSC_VER)
        VCKLog::Info  ("Platform", "Compiler: MSVC " + std::to_string(_MSC_VER));
#endif
    }

    // -------------------------------------------------------------------------
    //  [2] Logging level tour
    // -------------------------------------------------------------------------
    static void PrintLoggingLevels()
    {
        VCKLog::Notice("Log", "--- Section 2: VCKLog levels ---");
        VCKLog::Info  ("Log", "Info line (debug-gated; hidden when cfg.debug=false)");
        VCKLog::Notice("Log", "Notice line (always visible; user-actionable events)");
        VCKLog::Warn  ("Log", "Warn line (always visible; recoverable issues)");
        VCKLog::Error ("Log", "Error line (always visible; failures)");
    }

    // -------------------------------------------------------------------------
    //  [3] Debug gating
    // -------------------------------------------------------------------------
    static void PrintDebugGating()
    {
        VCKLog::Notice("Debug", "--- Section 3: cfg.debug gating ---");
        VCKLog::Notice("Debug", "debug=true -> you can see the Info line below:");
        VCKLog::SetDebug(true);
        VCKLog::Info  ("Debug", "  visible with debug=true");
        VCKLog::SetDebug(false);
        VCKLog::Notice("Debug", "debug=false -> the next Info line is SUPPRESSED:");
        VCKLog::Info  ("Debug", "  invisible with debug=false");
        VCKLog::Notice("Debug", "(the line above was Info; if you did not see it, gating works)");
        // Restore debug=true for the rest of the tour so device-property
        // Info lines remain visible.
        VCKLog::SetDebug(true);
    }

    // -------------------------------------------------------------------------
    //  [4] Dedup
    // -------------------------------------------------------------------------
    static void PrintDedup()
    {
        VCKLog::Notice("Dedup", "--- Section 4: dedup ---");
        for (int i = 0; i < 5; ++i) {
            VCKLog::Notice("Dedup", "Same line printed 5 times");
        }
        VCKLog::Notice("Dedup", "Different line: flushes the repeat counter");
    }

    // -------------------------------------------------------------------------
    //  [5] VK_CHECK fail-loud
    // -------------------------------------------------------------------------
    static void PrintVkCheckFailure()
    {
        VCKLog::Notice("VK_CHECK", "--- Section 5: fail-loud VK_CHECK ---");
        VCKLog::Notice("VK_CHECK", "The next line simulates a Vulkan failure (VK_ERROR_DEVICE_LOST=-4);");
        VCKLog::Notice("VK_CHECK", "even though debug=false now, the Error line MUST be visible (rule 14).");
        VCKLog::SetDebug(false);
        VK_CHECK(VK_ERROR_DEVICE_LOST);   // routes to VCKLog::Error
        VCKLog::SetDebug(true);
    }

    // -------------------------------------------------------------------------
    //  [6] Device dump
    // -------------------------------------------------------------------------
    static const char* VendorName(uint32_t vendorId)
    {
        switch (vendorId) {
            case 0x1002: return "AMD";
            case 0x10DE: return "NVIDIA";
            case 0x8086: return "Intel";
            case 0x13B5: return "ARM";
            case 0x1010: return "ImgTec";
            case 0x5143: return "Qualcomm";
            case 0x106B: return "Apple";
            default:     return "Unknown";
        }
    }

    static const char* DeviceTypeName(VkPhysicalDeviceType t)
    {
        switch (t) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
            default:                                     return "Other";
        }
    }

    static void PrintDeviceInfo()
    {
        VCKLog::Notice("Device", "--- Section 6: GPU properties ---");
        VkPhysicalDevice pdev = device.GetPhysicalDevice();
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pdev, &props);

        VCKLog::Notice("Device", std::string("Name       : ") + props.deviceName);
        VCKLog::Notice("Device", std::string("Type       : ") + DeviceTypeName(props.deviceType));
        VCKLog::Notice("Device", std::string("Vendor     : ") + VendorName(props.vendorID));
        {
            std::ostringstream os;
            os << "API        : " << VK_API_VERSION_MAJOR(props.apiVersion)
               << "." << VK_API_VERSION_MINOR(props.apiVersion)
               << "." << VK_API_VERSION_PATCH(props.apiVersion);
            VCKLog::Notice("Device", os.str());
        }
        VCKLog::Info("Device", "maxImageDimension2D = " + std::to_string(props.limits.maxImageDimension2D));
        VCKLog::Info("Device", "maxPerStageResources = " + std::to_string(props.limits.maxPerStageResources));

        const VkSampleCountFlags colourSamples = props.limits.framebufferColorSampleCounts;
        const VkSampleCountFlags depthSamples  = props.limits.framebufferDepthSampleCounts;
        auto maxSamples = [](VkSampleCountFlags f) -> int {
            if (f & VK_SAMPLE_COUNT_64_BIT) return 64;
            if (f & VK_SAMPLE_COUNT_32_BIT) return 32;
            if (f & VK_SAMPLE_COUNT_16_BIT) return 16;
            if (f & VK_SAMPLE_COUNT_8_BIT)  return 8;
            if (f & VK_SAMPLE_COUNT_4_BIT)  return 4;
            if (f & VK_SAMPLE_COUNT_2_BIT)  return 2;
            return 1;
        };
        VCKLog::Notice("Device", "Max MSAA (colour) = " + std::to_string(maxSamples(colourSamples)) + "x");
        VCKLog::Notice("Device", "Max MSAA (depth)  = " + std::to_string(maxSamples(depthSamples))  + "x");
    }

    // -------------------------------------------------------------------------
    //  [7] Surface + swapchain caps
    // -------------------------------------------------------------------------
    static void PrintSurfaceCaps()
    {
        VCKLog::Notice("Surface", "--- Section 7: surface / swapchain capabilities ---");
        VkPhysicalDevice pdev = device.GetPhysicalDevice();
        VkSurfaceKHR     surf = context.GetSurface();

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &formatCount, formats.data());
        VCKLog::Notice("Surface", std::to_string(formatCount) + " surface format(s) supported");
        for (size_t i = 0; i < formats.size() && i < 3; ++i) {
            VCKLog::Info("Surface", "  format[" + std::to_string(i) + "] = "
                        + std::to_string(formats[i].format) + ", colorSpace="
                        + std::to_string(formats[i].colorSpace));
        }

        uint32_t presentCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(pdev, surf, &presentCount, nullptr);
        std::vector<VkPresentModeKHR> modes(presentCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(pdev, surf, &presentCount, modes.data());
        VCKLog::Notice("Surface", std::to_string(presentCount) + " present mode(s) supported");
        for (VkPresentModeKHR m : modes) {
            const char* name = "Unknown";
            switch (m) {
                case VK_PRESENT_MODE_IMMEDIATE_KHR:    name = "Immediate";    break;
                case VK_PRESENT_MODE_MAILBOX_KHR:      name = "Mailbox";      break;
                case VK_PRESENT_MODE_FIFO_KHR:         name = "Fifo";         break;
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR: name = "Fifo-relaxed"; break;
                default: break;
            }
            VCKLog::Info("Surface", std::string("  ") + name);
        }
    }

    // -------------------------------------------------------------------------
    //  [8] Memory heap dump
    // -------------------------------------------------------------------------
    static void PrintMemoryHeaps()
    {
        VCKLog::Notice("Memory", "--- Section 8: memory heaps ---");
        VkPhysicalDevice pdev = device.GetPhysicalDevice();
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(pdev, &mem);

        for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
            const VkMemoryHeap& h = mem.memoryHeaps[i];
            const bool local   = (h.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            VCKLog::Notice("Memory", "Heap " + std::to_string(i) + ": " + ByteUnits(h.size)
                        + (local ? " (device-local)" : " (host-visible)"));
        }
    }

    // -------------------------------------------------------------------------
    //  [9] Pointer to DebugTimeline
    // -------------------------------------------------------------------------
    static void PrintDebugTimelinePointer()
    {
        VCKLog::Notice("Timeline", "--- Section 9: DebugTimeline (per-frame profiling) ---");
        VCKLog::Notice("Timeline", "See DebugTimelineExample for the per-frame CPU/GPU span recorder.");
        VCKLog::Notice("Timeline", "HandleLiveResize already emits a span when timeline is attached.");
    }

    // -------------------------------------------------------------------------
    //  Lifecycle
    // -------------------------------------------------------------------------
    void Init()
    {
        // Boot with debug=true so every Info line on init is visible.
        WindowCreateInfo wci;
        wci.title     = title;
        wci.width     = 640;
        wci.height    = 360;
        wci.resizable = false;                  // resize not needed for a headless tour
        if (!window.Create(wci)) return;

        Config cfg;
        cfg.debug = true;                       // show all init chatter
        context.Initialize(window, cfg);
        device .Initialize(context);

        VCKLog::Notice("Showcase", "============================================");
        VCKLog::Notice("Showcase", " VCK Debug Showcase - starting guided tour");
        VCKLog::Notice("Showcase", "============================================");
        Sleep(250);
    }

    void Run()
    {
        Init();

        PrintPlatform();            Sleep(400);
        PrintLoggingLevels();       Sleep(400);
        PrintDebugGating();         Sleep(400);
        PrintDedup();               Sleep(400);
        PrintVkCheckFailure();      Sleep(400);
        PrintDeviceInfo();          Sleep(400);
        PrintSurfaceCaps();         Sleep(400);
        PrintMemoryHeaps();         Sleep(400);
        PrintDebugTimelinePointer();

        VCKLog::Notice("Showcase", "============================================");
        VCKLog::Notice("Showcase", " Tour complete. Close the window to exit.");
        VCKLog::Notice("Showcase", "============================================");

        // Keep the window open so the user can read the console at their own
        // pace.  We do NOT render anything - this is a debug tour, not a draw
        // demo.  VCK::Window::ShouldClose() polls the OS event queue (GLFW).
        while (!window.ShouldClose()) {
            window.PollEvents();
            Sleep(16);
        }

        Shutdown();
    }

    void Shutdown()
    {
        device .Shutdown();
        context.Shutdown();
        window .Destroy();
    }

} // namespace VCK::DebugShowcaseExample
