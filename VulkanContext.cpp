#include "VulkanContext.h"
#include "VulkanHelpers.h"   // VK_CHECK, LogVk

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

namespace GTA_Sandbox {

    // ===========================================================================
    //  Public — Lifecycle
    // ===========================================================================

    bool VulkanContext::Initialize(HWND windowHandle, const std::string& appName) {
        LogVk("[VulkanContext] Initialize begin");

        if (!CreateInstance(appName)) {
            LogVk("[VulkanContext] FAILED: CreateInstance");
            return false;
        }

        if (ValidationEnabled) {
            if (!CreateDebugMessenger())
                LogVk("[VulkanContext] WARNING: debug messenger unavailable — continuing");
        }

        if (!CreateSurface(windowHandle)) {
            LogVk("[VulkanContext] FAILED: CreateSurface");
            return false;
        }

        LogVk("[VulkanContext] Initialize OK");
        return true;
    }

    void VulkanContext::Shutdown() {
        LogVk("[VulkanContext] Shutdown");

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
        LogVk("[VulkanContext] Shutdown complete");
    }

    // ===========================================================================
    //  Private — Instance
    // ===========================================================================

    bool VulkanContext::CreateInstance(const std::string& appName) {
        // --- Validation layers ---------------------------------------------------
        ValidationEnabled = k_WantValidation && CheckValidationLayerSupport();

        if (k_WantValidation && !ValidationEnabled)
            LogVk("[VulkanContext] WARNING: Validation requested but VK_LAYER_KHRONOS_validation not found");

        // --- Extensions ----------------------------------------------------------
        EnabledExtensions = BuildRequiredExtensions();

        // --- App info ------------------------------------------------------------
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = appName.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "GTA_Sandbox";
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

        const char* validationLayer = VALIDATION_LAYER;
        if (ValidationEnabled) {
            createInfo.enabledLayerCount = 1;
            createInfo.ppEnabledLayerNames = &validationLayer;
            createInfo.pNext = &debugCreateInfo;
        }
        else {
            createInfo.enabledLayerCount = 0;
            createInfo.ppEnabledLayerNames = nullptr;
        }

        VkResult result = vkCreateInstance(&createInfo, nullptr, &Instance);
        if (result != VK_SUCCESS) {
            LogVk("[VulkanContext] vkCreateInstance failed: " + std::to_string(result));
            return false;
        }

        LogVk("[VulkanContext] VkInstance created — API 1.2, extensions: "
            + std::to_string(EnabledExtensions.size()));
        return true;
    }

    // ===========================================================================
    //  Private — Debug Messenger
    // ===========================================================================

    bool VulkanContext::CreateDebugMessenger() {
        fnCreateDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
        fnDestroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));

        if (!fnCreateDebugMessenger || !fnDestroyDebugMessenger) {
            LogVk("[VulkanContext] vkCreateDebugUtilsMessengerEXT not found in instance");
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
            LogVk("[VulkanContext] Failed to create debug messenger: " + std::to_string(result));
            return false;
        }

        LogVk("[VulkanContext] Debug messenger active");
        return true;
    }

    // ===========================================================================
    //  Private — Win32 Surface
    // ===========================================================================

    bool VulkanContext::CreateSurface(HWND windowHandle) {
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = windowHandle;
        createInfo.hinstance = GetModuleHandleA(nullptr);

        VkResult result = vkCreateWin32SurfaceKHR(Instance, &createInfo, nullptr, &Surface);
        if (result != VK_SUCCESS) {
            LogVk("[VulkanContext] vkCreateWin32SurfaceKHR failed: " + std::to_string(result));
            return false;
        }

        LogVk("[VulkanContext] Win32 surface created");
        return true;
    }

    // ===========================================================================
    //  Private — Helpers
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

    std::vector<const char*> VulkanContext::BuildRequiredExtensions() {
        std::vector<const char*> extensions;

        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);        // "VK_KHR_surface"
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);  // "VK_KHR_win32_surface"

        if (k_WantValidation)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // "VK_EXT_debug_utils"

        return extensions;
    }

    // ===========================================================================
    //  Static — Debug Callback
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

        OutputDebugStringA((prefix + pCallbackData->pMessage + "\n").c_str());

        return VK_FALSE;
    }

} // namespace GTA_Sandbox