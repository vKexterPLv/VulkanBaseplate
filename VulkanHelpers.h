#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake — do NOT redefine here.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <string>

// -----------------------------------------------------------------------------
//  VulkanBaseplate_EnsureConsole
//
//  Attaches a Windows console to the current process (if one isn't already
//  attached) and points stdout / stderr at it so every printf / fputs /
//  std::cout writes into a visible console window.
//
//  Safe to call multiple times — only the first call does any work.
//  Typical use:
//      int main() {
//          VulkanBaseplate_EnsureConsole();   // first thing
//          ...
//      }
// -----------------------------------------------------------------------------
inline void VulkanBaseplate_EnsureConsole()
{
    static bool s_consoleReady = false;
    if (s_consoleReady) return;

    // Try to attach to a parent console first (running from cmd.exe / VS).
    // If there isn't one, allocate a fresh one (GUI/WinMain scenarios).
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        if (!AllocConsole())
        {
            // Console is unavailable (e.g. service process).  OutputDebugStringA
            // is still wired up, so logs continue to reach the VS Output window.
            s_consoleReady = true;
            return;
        }
        SetConsoleTitleA("VulkanBaseplate - Log");
    }

    // Redirect the C runtime streams to the console.
    FILE* fOut = nullptr;
    FILE* fErr = nullptr;
    FILE* fIn  = nullptr;
    freopen_s(&fOut, "CONOUT$", "w", stdout);
    freopen_s(&fErr, "CONOUT$", "w", stderr);
    freopen_s(&fIn,  "CONIN$",  "r", stdin);

    // Unbuffered so log lines appear immediately as frames run.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    s_consoleReady = true;
}

// -----------------------------------------------------------------------------
//  LogVk — routes to BOTH the VS Output window (OutputDebugStringA) and the
//          attached Windows console window (via stdout).
//  Global scope — usable from any file, any namespace.
// -----------------------------------------------------------------------------
inline void LogVk(const std::string& message) {
    const std::string line = "[VK] " + message + "\n";
    OutputDebugStringA(line.c_str());
    // If no console is attached this is still safe — stdout is a no-op sink
    // for /SUBSYSTEM:WINDOWS without AllocConsole, and the real console
    // window once VulkanBaseplate_EnsureConsole() has been called.
    std::fputs(line.c_str(), stdout);
}

// -----------------------------------------------------------------------------
//  VK_CHECK(expr)
//
//  Pass a single VkResult expression.  Stringifies the call for the error log.
//  Returns bool — true on VK_SUCCESS, false otherwise.
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
