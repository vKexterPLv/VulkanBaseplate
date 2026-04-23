#pragma once

#include "../VCKCrossplatform.h"   // VCK_PLATFORM_* + VCK::Window

#if VCK_PLATFORM_WINDOWS
    // WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake - do NOT redefine here.
    #include <windows.h>
    #include <vulkan/vulkan_win32.h>
#endif

#include <vulkan/vulkan.h>
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
    //    - VkSurfaceKHR              (created via VCK::Window on every platform)
    //
    //  Does NOT own: physical device, logical device, swapchain - those live in
    //  VulkanDevice / VulkanSwapchain.
    //
    //  Platforms:
    //    - Cross-platform (recommended):  Initialize(VCK::Window&, ...).  Works
    //      on Windows / Linux / macOS; the Window reports its required instance
    //      extensions and creates the surface.
    //    - Windows-only (legacy):         Initialize(HWND, ...).  Compiled only
    //      when VCK_PLATFORM_WINDOWS is 1.  Kept so existing Windows-only code
    //      keeps building unchanged.
    // ---------------------------------------------------------------------------
    class VulkanContext {
    public:
        VulkanContext() = default;
        ~VulkanContext() = default;

        // Non-copyable, non-movable
        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        // -----------------------------------------------------------------------
        //  Lifecycle  (cross-platform)
        // -----------------------------------------------------------------------

        // Preferred, cross-platform entry point.  Creates instance + debug
        // messenger + surface.  Required surface extensions are discovered from
        // VCK::Window::RequiredInstanceExtensions().
        bool Initialize(const Window& window, const Config& cfg);
        bool Initialize(const Window& window, const std::string& appName);

        // -----------------------------------------------------------------------
        //  Lifecycle  (Windows legacy)
        // -----------------------------------------------------------------------
#if VCK_PLATFORM_WINDOWS
        // Windows-only: build the surface via vkCreateWin32SurfaceKHR directly.
        // Kept for backward compatibility; new code should use the Window form.
        bool Initialize(HWND windowHandle, const Config& cfg);
        bool Initialize(HWND windowHandle, const std::string& appName);
#endif

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
        bool CreateInstance(const std::string& appName,
                            const std::vector<const char*>& surfaceExtensions);
        bool CreateDebugMessenger();
        bool CreateSurface(const Window& window);
#if VCK_PLATFORM_WINDOWS
        bool CreateSurface(HWND windowHandle);
#endif

        bool CheckValidationLayerSupport();

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
