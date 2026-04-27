// ─────────────────────────────────────────────────────────────────────────────
//  VmaImpl.cpp - VMA implementation translation unit
//
//  VMA is a single-header library. VMA_IMPLEMENTATION must be defined in
//  exactly ONE .cpp file across the entire project. This file is that one TU.
//  All other files just #include "vk_mem_alloc.h" normally (no define needed).
//
//  Source: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
//  Drop vk_mem_alloc.h next to this file (src/render/vulkan/).
// ─────────────────────────────────────────────────────────────────────────────

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"