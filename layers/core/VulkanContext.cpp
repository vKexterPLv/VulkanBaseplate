#include "VulkanContext.h"
#include "VulkanHelpers.h"   // VK_CHECK, LogVk

#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  Debug build toggle
//  Define VULKAN_VALIDATION in your build system (debug config only).
//  e.g. premake5:  filter "configurations:Debug"  defines { "VULKAN_VALIDATION" }
// ---------------------------------------------------------------------------
#ifdef VULKAN_VALIDATION
static constexpr bool k_WantValidation = true;
#else
static constexpr bool k_WantValidation = false;
#endif

namespace VCK {

    // ===========================================================================
    //  Public - Lifecycle
    // ===========================================================================

    // ---------------------------------------------------------------------
    //  Cross-platform path (VCK::Window)
    // ---------------------------------------------------------------------
    bool VulkanContext::Initialize(const Window& window, const std::string& appName) {
        Config cfg;
        cfg.context.appName = appName;
        return Initialize(window, cfg);
    }

    bool VulkanContext::Initialize(const Window& window, const Config& cfg) {
        // cfg.debug flips verbose Info-level logging for the whole process.
        // Done first so the rest of init respects it.
        VCKLog::SetDebug(cfg.debug);

        VCKLog::Info("Context", std::string("Initialize begin (") + VCK_PLATFORM_NAME + ")");

        m_CfgContext = cfg.context;

        // Surface extensions are platform-specific; ask the window backend
        // (VK_KHR_surface + VK_KHR_win32/xcb/xlib/wayland/metal).
        const auto surfaceExts = Window::RequiredInstanceExtensions();

        if (!CreateInstance(cfg.context.appName, surfaceExts)) {
            VCKLog::Error("Context", "CreateInstance failed");
            return false;
        }

        if (ValidationEnabled) {
            if (!CreateDebugMessenger())
                VCKLog::Warn("Context", "debug messenger unavailable - continuing");
        }

        if (!CreateSurface(window)) {
            VCKLog::Error("Context", "CreateSurface failed");
            return false;
        }

        VCKLog::Notice("Context", std::string("Initialized on ") + VCK_PLATFORM_NAME);
        return true;
    }

#if VCK_PLATFORM_WINDOWS
    // ---------------------------------------------------------------------
    //  Windows legacy path (HWND)
    // ---------------------------------------------------------------------
    bool VulkanContext::Initialize(HWND windowHandle, const std::string& appName) {
        Config cfg;
        cfg.context.appName = appName;
        return Initialize(windowHandle, cfg);
    }

    bool VulkanContext::Initialize(HWND windowHandle, const Config& cfg) {
        VCKLog::SetDebug(cfg.debug);

        VCKLog::Info("Context", "Initialize begin (HWND legacy)");

        m_CfgContext = cfg.context;

        const std::vector<const char*> surfaceExts = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        };

        if (!CreateInstance(cfg.context.appName, surfaceExts)) {
            VCKLog::Error("Context", "CreateInstance failed");
            return false;
        }

        if (ValidationEnabled) {
            if (!CreateDebugMessenger())
                VCKLog::Warn("Context", "debug messenger unavailable - continuing");
        }

        if (!CreateSurface(windowHandle)) {
            VCKLog::Error("Context", "CreateSurface failed");
            return false;
        }

        VCKLog::Notice("Context", "Initialized on Windows (HWND legacy)");
        return true;
    }
#endif

    void VulkanContext::Shutdown() {
        VCKLog::Info("Context", "Shutdown");

        // Order matters: surface → debug messenger → instance
        if (Surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(Instance, Surface, nullptr);
            Surface = VK_NULL_HANDLE;
        }

        if (DebugMessenger != VK_NULL_HANDLE && fnDestroyDebugMessenger) {
            fnDestroyDebugMessenger(Instance, DebugMessenger, nullptr);
            DebugMessenger = VK_NULL_HANDLE;
        }

        if (Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(Instance, nullptr);
            Instance = VK_NULL_HANDLE;
        }

        EnabledExtensions.clear();
        VCKLog::Info("Context", "Shutdown complete");
    }

    // ===========================================================================
    //  Private - Instance
    // ===========================================================================

    bool VulkanContext::CreateInstance(const std::string& appName,
                                       const std::vector<const char*>& surfaceExtensions) {
        // --- Validation layers ---------------------------------------------------
        // Validation is compiled-in when VULKAN_VALIDATION is defined AND the
        // user has not disabled it via cfg.context.enableValidation.
        const bool wantValidation = k_WantValidation && m_CfgContext.enableValidation;
        ValidationEnabled = wantValidation && CheckValidationLayerSupport();

        // (Mismatch warning is emitted post-vkCreateInstance below as part of the
        //  R23 extension-transparency block, so the log order matches the user's
        //  mental model: one consolidated 'Context' Notice/Warn block per init.)

        // --- Extensions ----------------------------------------------------------
        // Platform-specific surface extensions are supplied by the caller (either
        // VCK::Window::RequiredInstanceExtensions() on the cross-platform path,
        // or a hand-rolled Win32 list on the HWND legacy path).  Debug utils is
        // added when validation is enabled.
        EnabledExtensions = surfaceExtensions;
        if (ValidationEnabled)
            EnabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        for (const char* extra : m_CfgContext.extraInstanceExtensions)
            EnabledExtensions.push_back(extra);

        // --- App info ------------------------------------------------------------
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = appName.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "VCK";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        // --- Debug messenger info (chained for instance create/destroy) ----------
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (ValidationEnabled) {
            debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugCreateInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = DebugCallback;
        }

        // --- Instance create info ------------------------------------------------
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(EnabledExtensions.size());
        createInfo.ppEnabledExtensionNames = EnabledExtensions.data();

        // Build the final layer list: validation (if requested + available) +
        // user-supplied extras.  pNext chain only wires the debug messenger when
        // validation actually came up.
        std::vector<const char*> enabledLayers;
        if (ValidationEnabled)
            enabledLayers.push_back(VALIDATION_LAYER);
        for (const char* extra : m_CfgContext.extraInstanceLayers)
            enabledLayers.push_back(extra);

        createInfo.enabledLayerCount   = static_cast<uint32_t>(enabledLayers.size());
        createInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();
        if (ValidationEnabled)
            createInfo.pNext = &debugCreateInfo;

        VkResult result = vkCreateInstance(&createInfo, nullptr, &Instance);
        if (result != VK_SUCCESS) {
            VCKLog::Error("Context", "vkCreateInstance failed: " + std::to_string(result));
            return false;
        }

        // Rule 23: every instance extension VCK enabled is announced by name,
        // including who asked for it (surface, validation, user) so the user
        // can grep the log for "ext enabled" and see exactly what's running.
        for (const char* ext : surfaceExtensions)
            VCKLog::Notice("Context", std::string("ext enabled (surface): ") + ext);
        if (ValidationEnabled)
            VCKLog::Notice("Context", std::string("ext enabled (validation): ") + VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        for (const char* extra : m_CfgContext.extraInstanceExtensions)
            VCKLog::Notice("Context", std::string("ext enabled (cfg.extraInstanceExtensions): ") + extra);
        for (const char* layer : enabledLayers)
            VCKLog::Notice("Context", std::string("layer enabled: ") + layer);
        if (k_WantValidation && m_CfgContext.enableValidation && !ValidationEnabled)
            VCKLog::Warn("Context", "validation requested but VK_LAYER_KHRONOS_validation not present - continuing without it");

        VCKLog::Info("Context", "VkInstance created - API 1.2, extensions: "
            + std::to_string(EnabledExtensions.size()));
        return true;
    }

    // ===========================================================================
    //  Private - Debug Messenger
    // ===========================================================================

    bool VulkanContext::CreateDebugMessenger() {
        fnCreateDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
        fnDestroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));

        if (!fnCreateDebugMessenger || !fnDestroyDebugMessenger) {
            VCKLog::Error("Context", "vkCreateDebugUtilsMessengerEXT not found in instance");
            return false;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;
        createInfo.pUserData = nullptr;

        VkResult result = fnCreateDebugMessenger(Instance, &createInfo, nullptr, &DebugMessenger);
        if (result != VK_SUCCESS) {
            VCKLog::Error("Context", "Failed to create debug messenger: " + std::to_string(result));
            return false;
        }

        VCKLog::Notice("Context", "Debug messenger active");
        return true;
    }

    // ===========================================================================
    //  Private - Surface creation
    //    Cross-platform path goes through VCK::Window.  The Win32 legacy path
    //    calls vkCreateWin32SurfaceKHR directly for backward compatibility.
    // ===========================================================================

    bool VulkanContext::CreateSurface(const Window& window) {
        VkResult result = window.CreateSurface(Instance, &Surface);
        if (result != VK_SUCCESS) {
            VCKLog::Error("Context", "VCK::Window::CreateSurface failed: " + std::to_string(result));
            return false;
        }
        VCKLog::Notice("Context", std::string("Surface created (") + VCK_PLATFORM_NAME + ")");
        return true;
    }

#if VCK_PLATFORM_WINDOWS
    bool VulkanContext::CreateSurface(HWND windowHandle) {
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = windowHandle;
        createInfo.hinstance = GetModuleHandleA(nullptr);

        VkResult result = vkCreateWin32SurfaceKHR(Instance, &createInfo, nullptr, &Surface);
        if (result != VK_SUCCESS) {
            VCKLog::Error("Context", "vkCreateWin32SurfaceKHR failed: " + std::to_string(result));
            return false;
        }

        VCKLog::Notice("Context", "Win32 surface created");
        return true;
    }
#endif

    // ===========================================================================
    //  Private - Helpers
    // ===========================================================================

    bool VulkanContext::CheckValidationLayerSupport() {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());

        for (const auto& layer : available) {
            if (std::string(layer.layerName) == VALIDATION_LAYER)
                return true;
        }
        return false;
    }

    // ===========================================================================
    //  Static - Debug Callback
    // ===========================================================================

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT             /*messageType*/,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       /*pUserData*/)
    {
        std::string prefix;
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   prefix = "[VULKAN ERROR]   ";
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) prefix = "[VULKAN WARNING] ";
        else                                                                          prefix = "[VULKAN INFO]    ";

        const std::string line = prefix + pCallbackData->pMessage + "\n";
#if VCK_PLATFORM_WINDOWS
        OutputDebugStringA(line.c_str());
#else
        std::fputs(line.c_str(), stderr);
#endif

        return VK_FALSE;
    }

} // namespace VCK