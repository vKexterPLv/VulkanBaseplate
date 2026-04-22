#pragma once

// -----------------------------------------------------------------------------
//  VCK::Window  -  thin cross-platform window + Vulkan-surface facade.
//
//  The goal is NOT to reimplement GLFW.  It is to give user code a VCK-native
//  call form so #include <GLFW/glfw3.h> and #include <GLFW/glfw3native.h>
//  never appear in user files.  GLFW is already cross-platform
//  (Windows / Linux / macOS), so the same facade source works on all three -
//  when VCK is ported off Windows, nothing in user code changes.  A future
//  native X11 / XCB / Cocoa backend would plug in behind the same API.
//
//  Typical use:
//
//      VCK::Window win;
//      VCK::WindowCreateInfo wci{ 1280, 720, "My App", true };
//      if (!win.Create(wci)) return -1;
//
//      // Vulkan instance needs the window system's required extensions:
//      auto wantedExts = VCK::Window::RequiredInstanceExtensions();
//      // ...pass them to VulkanContext cfg.context.extraInstanceExtensions.
//
//      // Create the VkSurfaceKHR on the instance (owned by the caller):
//      VkSurfaceKHR surface = VK_NULL_HANDLE;
//      win.CreateSurface(instance, &surface);
//
//      // Main loop:
//      while (!win.ShouldClose()) {
//          win.PollEvents();
//          int fbw, fbh;  win.GetFramebufferSize(fbw, fbh);
//          // ... render frame ...
//      }
//      win.Destroy();
//
//  Implementation is intentionally header-only and includes <GLFW/glfw3.h>
//  here, which means only *one* translation unit is allowed to include
//  VCKWindow.h if user code links GLFW statically and is sensitive to
//  multiple-definition errors.  Our amalgam (VCK.h) includes it once; user
//  code getting the API via "VCK.h" pays no extra cost.
// -----------------------------------------------------------------------------

#include <vulkan/vulkan.h>

// GLFW wants to see VK_KHR_surface symbols to wire glfwCreateWindowSurface;
// defining GLFW_INCLUDE_VULKAN asks it to include vulkan.h for us (already
// included above, so this is just belt-and-braces).
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <vector>

#include "VulkanHelpers.h"   // VCKLog

namespace VCK {

struct WindowCreateInfo
{
    int         width     = 1280;
    int         height    = 720;
    std::string title     = "VCK App";
    bool        resizable = true;
};

class Window
{
public:
    Window() = default;
    ~Window() { Destroy(); }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates the window.  Returns false (and logs) on failure.  Safe to call
    // once per Window instance; call Destroy() before creating another.
    bool Create(const WindowCreateInfo& info)
    {
        if (m_Handle)
        {
            VCKLog::Warn("Window", "Create() called twice without Destroy(); ignoring");
            return true;
        }

        if (!s_GlfwInit)
        {
            if (!glfwInit())
            {
                VCKLog::Error("Window", "glfwInit failed");
                return false;
            }
            s_GlfwInit = true;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE,  info.resizable ? GLFW_TRUE : GLFW_FALSE);

        m_Handle = glfwCreateWindow(info.width, info.height,
                                    info.title.c_str(),
                                    /*monitor*/ nullptr,
                                    /*share*/   nullptr);
        if (!m_Handle)
        {
            VCKLog::Error("Window", "glfwCreateWindow returned null");
            return false;
        }

        VCKLog::Info("Window",
            "Created " + std::to_string(info.width) + "x" +
            std::to_string(info.height) + " '" + info.title + "'");
        return true;
    }

    void Destroy()
    {
        if (m_Handle)
        {
            glfwDestroyWindow(m_Handle);
            m_Handle = nullptr;
        }
        // GLFW itself is terminated by the last Window going away only if the
        // caller explicitly asks for it - some apps create a second window
        // later.  Leave glfwTerminate to the user (Legacy GLFW pattern).
    }

    // Per-frame helpers.
    bool ShouldClose() const { return m_Handle && glfwWindowShouldClose(m_Handle); }
    void PollEvents()  const { glfwPollEvents(); }
    void WaitEvents()  const { glfwWaitEvents(); }

    void GetFramebufferSize(int& width, int& height) const
    {
        if (m_Handle) glfwGetFramebufferSize(m_Handle, &width, &height);
        else          { width = 0; height = 0; }
    }

    // Vulkan integration --------------------------------------------------------

    // Instance extensions the backend needs for VkSurfaceKHR.  Always includes
    // VK_KHR_surface plus a platform-specific one (VK_KHR_win32_surface on
    // Windows, VK_KHR_xcb_surface / VK_KHR_xlib_surface / VK_KHR_wayland_surface
    // on Linux, VK_EXT_metal_surface on macOS).  Feed this into
    // cfg.context.extraInstanceExtensions.
    static std::vector<const char*> RequiredInstanceExtensions()
    {
        if (!s_GlfwInit)
        {
            if (!glfwInit()) return {};
            s_GlfwInit = true;
        }
        uint32_t count = 0;
        const char** raw = glfwGetRequiredInstanceExtensions(&count);
        return std::vector<const char*>(raw, raw + count);
    }

    // Creates the VkSurfaceKHR for this window on `instance`.  Caller owns the
    // surface and must destroy it before the instance.  Returns VkResult from
    // glfwCreateWindowSurface.
    VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* outSurface) const
    {
        if (!m_Handle || !outSurface)
        {
            VCKLog::Error("Window", "CreateSurface called before Create() or with null outSurface");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        return glfwCreateWindowSurface(instance, m_Handle, nullptr, outSurface);
    }

    // Escape hatch for anyone who *does* want the raw handle.  Kept so
    // VCK::Window never imposes limits on what advanced users can do.
    GLFWwindow* NativeHandle() const { return m_Handle; }

private:
    GLFWwindow* m_Handle = nullptr;

    // One-shot flag so multiple Windows don't re-init GLFW.
    inline static bool s_GlfwInit = false;
};

} // namespace VCK
