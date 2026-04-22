#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake - do NOT redefine here.
#include <windows.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include "VulkanHelpers.h"   // VCK::Config

#include <string>
#include <vector>

namespace VCK {

    // ---------------------------------------------------------------------------
    //  VulkanContext
    //
    //  Owns:
    //    - VkInstance
    //    - VkDebugUtilsMessengerEXT  (debug builds only)
    //    - VkSurfaceKHR              (Win32 surface)
    //
    //  Does NOT own: physical device, logical device, swapchain - those live in
    //  VulkanDevice / VulkanSwapchain.
    // ---------------------------------------------------------------------------
    class VulkanContext {
    public:
        VulkanContext() = default;
        ~VulkanContext() = default;

        // Non-copyable, non-movable
        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        // -----------------------------------------------------------------------
        //  Lifecycle
        // -----------------------------------------------------------------------

        // Creates instance + debug messenger + surface.
        // Returns false and logs reason on failure.
        //
        // Preferred form: pass a VCK::Config with whatever appName /
        // validation / extra layers-and-extensions you want.  Zero-config
        // form keeps working - it just inflates appName into a default Config.
        bool Initialize(HWND windowHandle, const Config& cfg);
        bool Initialize(HWND windowHandle, const std::string& appName);

        // Destroys surface, debug messenger, instance - in correct order.
        void Shutdown();

        // -----------------------------------------------------------------------
        //  Accessors  (read-only - other systems borrow these, never own them)
        // -----------------------------------------------------------------------
        VkInstance   GetInstance() const { return Instance; }
        VkSurfaceKHR GetSurface()  const { return Surface; }

        // Returns all instance extensions that were actually enabled.
        const std::vector<const char*>& GetEnabledExtensions() const { return EnabledExtensions; }

        bool IsValidationEnabled() const { return ValidationEnabled; }

    private:
        // -----------------------------------------------------------------------
        //  Internal helpers
        // -----------------------------------------------------------------------
        bool CreateInstance(const std::string& appName);
        bool CreateDebugMessenger();
        bool CreateSurface(HWND windowHandle);

        bool CheckValidationLayerSupport();
        std::vector<const char*> BuildRequiredExtensions();

        // Vulkan debug callback - static so it matches PFN_vkDebugUtilsMessengerCallbackEXT
        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT             messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData);

        // Dynamic loader helpers (vkCreateDebugUtilsMessengerEXT is not in the
        // static Vulkan lib - must be loaded at runtime via vkGetInstanceProcAddr)
        PFN_vkCreateDebugUtilsMessengerEXT  fnCreateDebugMessenger = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT fnDestroyDebugMessenger = nullptr;

        // -----------------------------------------------------------------------
        //  State
        // -----------------------------------------------------------------------
        VkInstance                 Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT   DebugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR               Surface = VK_NULL_HANDLE;

        std::vector<const char*>   EnabledExtensions;
        bool                       ValidationEnabled = false;

        // Snapshot of cfg.context (appName / validation / extra layers+exts).
        Config::ContextCfg         m_CfgContext;

        // Validation layer name - single source of truth
        static constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
    };

} // namespace VCK