#pragma once

// -----------------------------------------------------------------------------
//  VCKCrossplatform.h
//  -------------------------------------------------------------------------
//  The one place in VCK that knows which operating system it is running on.
//
//  Goals
//  ~~~~~
//  1. Give user code a VCK-native window + surface API so
//        #include <GLFW/glfw3.h>
//        #include <GLFW/glfw3native.h>
//        #include <windows.h>
//     never appear in user files.  Users keep writing the promised single
//     `#include "VCK.h"`.
//
//  2. Keep each platform's implementation physically separate inside
//     VCKCrossplatform.cpp via VCK_PLATFORM_WINDOWS / VCK_PLATFORM_LINUX /
//     VCK_PLATFORM_MACOS branches.  Today all three branches happen to use
//     GLFW as their backend, but they are laid out as independent code paths
//     so a future "pure Win32" or "pure XCB" backend can replace one branch
//     without touching the others.
//
//  3. Be the only file anywhere in the tree that performs platform detection.
//     Every other VCK header and cpp file is platform-agnostic and goes
//     through this API.
//
//  Adding a new platform
//  ~~~~~~~~~~~~~~~~~~~~~
//  - Define a new VCK_PLATFORM_* macro block below.
//  - In VCKCrossplatform.cpp add a `#elif VCK_PLATFORM_<YOURS>` branch that
//    implements the Window member functions for that OS.
//  - Update VCK::Window::RequiredInstanceExtensions() to return the correct
//    surface extensions for the new platform.
//  - Nothing else in VCK needs to change.
//
//  Typical use
//  ~~~~~~~~~~~
//      VCK::Window win;
//      VCK::WindowCreateInfo wci{ 1280, 720, "My App", /*resizable=*/true };
//      if (!win.Create(wci)) return -1;
//
//      auto wantedExts = VCK::Window::RequiredInstanceExtensions();
//      // ... pass wantedExts into cfg.context.extraInstanceExtensions ...
//
//      VkSurfaceKHR surface = VK_NULL_HANDLE;
//      win.CreateSurface(instance, &surface);
//
//      while (!win.ShouldClose()) {
//          win.PollEvents();
//          int fbw, fbh;  win.GetFramebufferSize(fbw, fbh);
//          // ... render frame ...
//      }
//      win.Destroy();
// -----------------------------------------------------------------------------

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

// -- Platform detection --------------------------------------------------------
// Exactly one of these is 1.  User code and other VCK TUs can #if on these.
#if defined(_WIN32) || defined(_WIN64)
    #define VCK_PLATFORM_WINDOWS 1
    #define VCK_PLATFORM_LINUX   0
    #define VCK_PLATFORM_MACOS   0
    #define VCK_PLATFORM_NAME    "Windows"
#elif defined(__linux__)
    #define VCK_PLATFORM_WINDOWS 0
    #define VCK_PLATFORM_LINUX   1
    #define VCK_PLATFORM_MACOS   0
    #define VCK_PLATFORM_NAME    "Linux"
#elif defined(__APPLE__)
    #define VCK_PLATFORM_WINDOWS 0
    #define VCK_PLATFORM_LINUX   0
    #define VCK_PLATFORM_MACOS   1
    #define VCK_PLATFORM_NAME    "macOS"
#else
    #error "VCK: this platform is not supported yet.  Add a new VCK_PLATFORM_* branch in VCKCrossplatform.h and a matching #elif in VCKCrossplatform.cpp."
#endif

namespace VCK {

// ----------------------------------------------------------------------------
//  WindowCreateInfo - parameters for VCK::Window::Create.
// ----------------------------------------------------------------------------
struct WindowCreateInfo
{
    int         width     = 1280;
    int         height    = 720;
    std::string title     = "VCK App";
    bool        resizable = true;
};

// ----------------------------------------------------------------------------
//  VCK::Window - cross-platform window + Vulkan-surface facade.
//
//  All member functions are defined in VCKCrossplatform.cpp inside platform-
//  specific #if blocks, so every platform's implementation is isolated from
//  the others.
//
//  The handle is held as an opaque `void*` so this header does not leak
//  <GLFW/glfw3.h> or <windows.h> into user translation units.  If you need
//  the raw native pointer (GLFWwindow*, HWND, xcb_window_t, etc.) call
//  NativeHandle() and cast to the type your platform uses.
// ----------------------------------------------------------------------------
class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // Creates the window.  Returns false (and logs via VCKLog) on failure.
    bool Create(const WindowCreateInfo& info);
    void Destroy();

    // Per-frame helpers.
    bool ShouldClose() const;
    void PollEvents()  const;
    void WaitEvents()  const;
    void GetFramebufferSize(int& width, int& height) const;

    // Vulkan integration.
    //
    // RequiredInstanceExtensions() returns the instance extensions the backend
    // needs for VkSurfaceKHR.  Always includes VK_KHR_surface plus one
    // platform-specific entry (win32 / xcb / xlib / wayland / metal).  Feed
    // this into cfg.context.extraInstanceExtensions before initialising
    // VulkanContext.
    static std::vector<const char*> RequiredInstanceExtensions();

    // Creates the VkSurfaceKHR for this window on `instance`.  Caller owns the
    // surface and must destroy it before the instance.
    VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* outSurface) const;

    // Raw native handle escape hatch.  Cast to the correct type:
    //   Windows: static_cast<GLFWwindow*>(win.NativeHandle())
    //   Linux:   static_cast<GLFWwindow*>(win.NativeHandle())
    // (Will change type per-platform once native backends replace GLFW.)
    void* NativeHandle() const { return m_Handle; }

private:
    void* m_Handle = nullptr;
};

} // namespace VCK
