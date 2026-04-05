#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake — do NOT redefine here.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <string>

// -----------------------------------------------------------------------------
//  LogVk — routes to VS Output window via OutputDebugStringA
//  Global scope — usable from any file, any namespace.
// -----------------------------------------------------------------------------
inline void LogVk(const std::string& message) {
    OutputDebugStringA(("[VK] " + message + "\n").c_str());
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
