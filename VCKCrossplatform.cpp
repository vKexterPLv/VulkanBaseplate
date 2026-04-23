// -----------------------------------------------------------------------------
//  VCKCrossplatform.cpp
//  -------------------------------------------------------------------------
//  The only .cpp file in VCK that contains platform-specific code.  Every
//  other TU compiles identically on every platform VCK supports.
//
//  Layout
//  ~~~~~~
//    [shared plumbing]    - platform-agnostic state (GLFW init latch)
//    #if VCK_PLATFORM_WINDOWS
//        Windows implementation
//    #elif VCK_PLATFORM_LINUX
//        Linux implementation
//    #elif VCK_PLATFORM_MACOS
//        macOS implementation  (stub today, ready for a backend)
//    #endif
//
//  All branches currently route through GLFW, which is itself cross-platform.
//  They are kept as separate branches (rather than one shared implementation)
//  so a native backend can replace one OS without perturbing the others -
//  e.g. Linux could switch to direct XCB while Windows stays on GLFW.
// -----------------------------------------------------------------------------

#include "VCKCrossplatform.h"

#include "core/VulkanHelpers.h"   // VCKLog

// GLFW_INCLUDE_VULKAN is also injected as a -D flag in build.bat so every TU
// that eventually pulls glfw3.h (directly or transitively) sees
// glfwCreateWindowSurface declared.  Defining it here too is belt-and-braces
// for TUs that include VCKCrossplatform.cpp directly or via a unity build.
#ifndef GLFW_INCLUDE_VULKAN
    #define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

// =============================================================================
//  Shared plumbing - runs on every platform.
// =============================================================================
namespace
{
    // One-shot latch so multiple VCK::Window instances do not re-init GLFW.
    bool g_GlfwInit = false;

    bool EnsureGlfwInitialized()
    {
        if (g_GlfwInit) return true;
        if (!glfwInit())
        {
            VCKLog::Error("Crossplatform",
                std::string("glfwInit() failed on ") + VCK_PLATFORM_NAME);
            return false;
        }
        g_GlfwInit = true;
        return true;
    }

    // ------------------------------------------------------------------
    //  Callback forwarders.
    //  Every platform backend happens to use GLFW today, which is strongly
    //  typed.  We hang the VCK::Window* off glfwSetWindowUserPointer and
    //  forward into its stored callback slots.  If a future backend swaps
    //  GLFW for XCB/Cocoa/Win32 it just needs to reproduce this routing.
    // ------------------------------------------------------------------
    void GlfwFramebufferSizeForwarder(GLFWwindow* w, int width, int height)
    {
        auto* self = static_cast<VCK::Window*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        // Auto-track the new size + set the resize latch before any user
        // callback runs, so a callback that reads Window::WasResized() /
        // Window::GetWidth() gets consistent values.
        VCK::SetWindowResizedInternal(*self, width, height);
        if (auto cb = self->GetFramebufferSizeCallback())
            cb(width, height);
    }

    void GlfwWindowRefreshForwarder(GLFWwindow* w)
    {
        auto* self = static_cast<VCK::Window*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        if (auto cb = self->GetWindowRefreshCallback())
            cb();
    }

    // Wires a freshly-created GLFWwindow into the forwarders above.
    // Called at the tail of each platform's Create() branch.
    void InstallGlfwCallbacks(GLFWwindow* w, VCK::Window* self)
    {
        glfwSetWindowUserPointer(w, self);
        glfwSetFramebufferSizeCallback(w, &GlfwFramebufferSizeForwarder);
        glfwSetWindowRefreshCallback  (w, &GlfwWindowRefreshForwarder);
    }
}

namespace VCK {

Window::~Window() { Destroy(); }

// Callback setters are identical on every platform because the backend is
// always GLFW today; the forwarders above do the dispatch.  If a future
// backend replaces GLFW on one OS, move these inside the matching #if block
// and register the equivalent native callbacks there.
void Window::SetFramebufferSizeCallback(FramebufferSizeCallback cb) { m_FbSizeCb  = cb; }
void Window::SetWindowRefreshCallback  (WindowRefreshCallback   cb) { m_RefreshCb = cb; }

// Internal helper used by the backend forwarders to update VCK::Window's
// private size + resize latch.  Kept out of the public API so users can't
// spoof a resize event.
void SetWindowResizedInternal(Window& w, int width, int height)
{
    w.m_Width   = width;
    w.m_Height  = height;
    w.m_Resized = true;
}

// =============================================================================
//  Windows implementation
// =============================================================================
#if VCK_PLATFORM_WINDOWS

bool Window::Create(const WindowCreateInfo& info)
{
    if (m_Handle)
    {
        VCKLog::Warn("Window",
            "Create() called twice without Destroy(); ignoring (Windows)");
        return true;
    }
    if (!EnsureGlfwInitialized()) return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  info.resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* w = glfwCreateWindow(info.width, info.height,
                                     info.title.c_str(),
                                     /*monitor*/ nullptr,
                                     /*share*/   nullptr);
    if (!w)
    {
        VCKLog::Error("Window", "glfwCreateWindow returned null (Windows)");
        return false;
    }
    m_Handle = w;
    InstallGlfwCallbacks(w, this);

    // Seed auto-tracked size from the real framebuffer.  On high-DPI /
    // retina displays this may differ from the requested logical size.
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(w, &fbw, &fbh);
    m_Width   = fbw;
    m_Height  = fbh;
    m_Resized = false;

    VCKLog::Info("Window",
        "Windows: created " + std::to_string(info.width) + "x" +
        std::to_string(info.height) + " '" + info.title + "'");
    return true;
}

void Window::Destroy()
{
    if (!m_Handle) return;
    glfwDestroyWindow(static_cast<GLFWwindow*>(m_Handle));
    m_Handle = nullptr;
}

bool Window::ShouldClose() const
{
    return m_Handle &&
        glfwWindowShouldClose(static_cast<GLFWwindow*>(m_Handle));
}

void Window::PollEvents() const { glfwPollEvents(); }
void Window::WaitEvents() const { glfwWaitEvents(); }

void Window::GetFramebufferSize(int& width, int& height) const
{
    if (m_Handle)
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Handle),
                               &width, &height);
    else { width = 0; height = 0; }
}

std::vector<const char*> Window::RequiredInstanceExtensions()
{
    if (!EnsureGlfwInitialized()) return {};
    uint32_t count = 0;
    const char** raw = glfwGetRequiredInstanceExtensions(&count);
    return std::vector<const char*>(raw, raw + count);
}

VkResult Window::CreateSurface(VkInstance instance,
                               VkSurfaceKHR* outSurface) const
{
    if (!m_Handle || !outSurface)
    {
        VCKLog::Error("Window",
            "CreateSurface called before Create() or with null outSurface (Windows)");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return glfwCreateWindowSurface(instance,
        static_cast<GLFWwindow*>(m_Handle), nullptr, outSurface);
}

// =============================================================================
//  Linux implementation
// =============================================================================
#elif VCK_PLATFORM_LINUX

bool Window::Create(const WindowCreateInfo& info)
{
    if (m_Handle)
    {
        VCKLog::Warn("Window",
            "Create() called twice without Destroy(); ignoring (Linux)");
        return true;
    }
    if (!EnsureGlfwInitialized()) return false;

    // On Linux GLFW auto-selects between X11 and Wayland at runtime (since
    // GLFW 3.4).  No source-level branching needed here - when VCK grows a
    // native XCB / Wayland backend it can replace this GLFW block entirely.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  info.resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* w = glfwCreateWindow(info.width, info.height,
                                     info.title.c_str(),
                                     /*monitor*/ nullptr,
                                     /*share*/   nullptr);
    if (!w)
    {
        VCKLog::Error("Window", "glfwCreateWindow returned null (Linux)");
        return false;
    }
    m_Handle = w;
    InstallGlfwCallbacks(w, this);

    // Seed auto-tracked size from the real framebuffer.  On high-DPI /
    // retina displays this may differ from the requested logical size.
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(w, &fbw, &fbh);
    m_Width   = fbw;
    m_Height  = fbh;
    m_Resized = false;

    VCKLog::Info("Window",
        "Linux: created " + std::to_string(info.width) + "x" +
        std::to_string(info.height) + " '" + info.title + "'");
    return true;
}

void Window::Destroy()
{
    if (!m_Handle) return;
    glfwDestroyWindow(static_cast<GLFWwindow*>(m_Handle));
    m_Handle = nullptr;
}

bool Window::ShouldClose() const
{
    return m_Handle &&
        glfwWindowShouldClose(static_cast<GLFWwindow*>(m_Handle));
}

void Window::PollEvents() const { glfwPollEvents(); }
void Window::WaitEvents() const { glfwWaitEvents(); }

void Window::GetFramebufferSize(int& width, int& height) const
{
    if (m_Handle)
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Handle),
                               &width, &height);
    else { width = 0; height = 0; }
}

std::vector<const char*> Window::RequiredInstanceExtensions()
{
    if (!EnsureGlfwInitialized()) return {};
    uint32_t count = 0;
    const char** raw = glfwGetRequiredInstanceExtensions(&count);
    // glfwGetRequiredInstanceExtensions returns VK_KHR_surface +
    // VK_KHR_xcb_surface / VK_KHR_xlib_surface / VK_KHR_wayland_surface
    // depending on the compositor picked at runtime.
    return std::vector<const char*>(raw, raw + count);
}

VkResult Window::CreateSurface(VkInstance instance,
                               VkSurfaceKHR* outSurface) const
{
    if (!m_Handle || !outSurface)
    {
        VCKLog::Error("Window",
            "CreateSurface called before Create() or with null outSurface (Linux)");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return glfwCreateWindowSurface(instance,
        static_cast<GLFWwindow*>(m_Handle), nullptr, outSurface);
}

// =============================================================================
//  macOS implementation (placeholder; ready for a backend)
// =============================================================================
#elif VCK_PLATFORM_MACOS

bool Window::Create(const WindowCreateInfo& info)
{
    if (m_Handle)
    {
        VCKLog::Warn("Window",
            "Create() called twice without Destroy(); ignoring (macOS)");
        return true;
    }
    if (!EnsureGlfwInitialized()) return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  info.resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* w = glfwCreateWindow(info.width, info.height,
                                     info.title.c_str(),
                                     /*monitor*/ nullptr,
                                     /*share*/   nullptr);
    if (!w)
    {
        VCKLog::Error("Window", "glfwCreateWindow returned null (macOS)");
        return false;
    }
    m_Handle = w;
    InstallGlfwCallbacks(w, this);

    // Seed auto-tracked size from the real framebuffer.  On high-DPI /
    // retina displays this may differ from the requested logical size.
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(w, &fbw, &fbh);
    m_Width   = fbw;
    m_Height  = fbh;
    m_Resized = false;

    VCKLog::Info("Window",
        "macOS: created " + std::to_string(info.width) + "x" +
        std::to_string(info.height) + " '" + info.title + "'");
    return true;
}

void Window::Destroy()
{
    if (!m_Handle) return;
    glfwDestroyWindow(static_cast<GLFWwindow*>(m_Handle));
    m_Handle = nullptr;
}

bool Window::ShouldClose() const
{
    return m_Handle &&
        glfwWindowShouldClose(static_cast<GLFWwindow*>(m_Handle));
}
void Window::PollEvents() const { glfwPollEvents(); }
void Window::WaitEvents() const { glfwWaitEvents(); }
void Window::GetFramebufferSize(int& width, int& height) const
{
    if (m_Handle)
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Handle),
                               &width, &height);
    else { width = 0; height = 0; }
}

std::vector<const char*> Window::RequiredInstanceExtensions()
{
    if (!EnsureGlfwInitialized()) return {};
    uint32_t count = 0;
    const char** raw = glfwGetRequiredInstanceExtensions(&count);
    return std::vector<const char*>(raw, raw + count);
}

VkResult Window::CreateSurface(VkInstance instance,
                               VkSurfaceKHR* outSurface) const
{
    if (!m_Handle || !outSurface)
    {
        VCKLog::Error("Window",
            "CreateSurface called before Create() or with null outSurface (macOS)");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return glfwCreateWindowSurface(instance,
        static_cast<GLFWwindow*>(m_Handle), nullptr, outSurface);
}

#endif

} // namespace VCK
