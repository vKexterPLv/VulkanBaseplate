#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake — do NOT redefine here.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <string>

// -----------------------------------------------------------------------------
//  LogVk — routes to BOTH the Visual Studio Output window
//          (OutputDebugStringA) and the console window (stdout / printf).
//  Global scope — usable from any file, any namespace.
//
//  Binaries built via example/build.bat use the default g++ console subsystem,
//  so stdout is already wired to whatever console launched the process — no
//  AllocConsole dance needed.
// -----------------------------------------------------------------------------
inline void LogVk(const std::string& message) {
    const std::string line = "[VK] " + message + "\n";
    OutputDebugStringA(line.c_str());
    std::fputs(line.c_str(), stdout);
    std::fflush(stdout);
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
