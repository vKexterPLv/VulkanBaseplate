// =============================================================================
//  VulkanModule.h
//
//  Single-header amalgam of the VulkanBaseplate Vulkan layer.
//  Including this file gives you every class declaration, struct, macro, and
//  constant across all 8 translation units.
//
//  SOURCE FILES ASSEMBLED
//  ──────────────────────
//  Headers   (.h)  : VulkanHelpers   VulkanContext   VulkanDevice
//                    VulkanSwapchain  VulkanBuffer    VulkanImage
//                    VulkanPipeline   VulkanCommand   VulkanSync
//
//  Implementations (.cpp) — function index at the bottom of this file:
//                    VmaImpl         VulkanContext    VulkanDevice
//                    VulkanSwapchain VulkanBuffer     VulkanImage
//                    VulkanPipeline  VulkanCommand    VulkanSync
//                    VulkanHelpers
//
//  INIT / SHUTDOWN ORDER
//  ─────────────────────
//  Init:      Context → Device → Swapchain → Pipeline → Command → Sync
//  Shutdown:  Sync → Command → Pipeline → Swapchain → Device → Context
//
//  QUICK NAMESPACE
//  ───────────────
//  Everything lives in:  namespace VulkanBaseplate { ... }
// =============================================================================

#pragma once

// ─── System / Vulkan prerequisites ───────────────────────────────────────────
// WIN32_LEAN_AND_MEAN and NOMINMAX are defined by premake — do NOT redefine.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include "vk_mem_alloc.h"

#include <string>
#include <vector>
#include <array>
#include <optional>
#include <cstdint>


// =============================================================================
//  ██╗  ██╗███████╗██╗     ██████╗ ███████╗██████╗ ███████╗
//  ██║  ██║██╔════╝██║     ██╔══██╗██╔════╝██╔══██╗██╔════╝
//  ███████║█████╗  ██║     ██████╔╝█████╗  ██████╔╝███████╗
//  ██╔══██║██╔══╝  ██║     ██╔═══╝ ██╔══╝  ██╔══██╗╚════██║
//  ██║  ██║███████╗███████╗██║     ███████╗██║  ██║███████║
//  ╚═╝  ╚═╝╚══════╝╚══════╝╚═╝     ╚══════╝╚═╝  ╚═╝╚══════╝
// =============================================================================

// -----------------------------------------------------------------------------
//  VulkanHelpers.h
//  Global logging utility and VK_CHECK macro.
//  No namespace — usable everywhere.
// -----------------------------------------------------------------------------

// LogVk — routes to VS Output window via OutputDebugStringA
inline void LogVk(const std::string& message) {
    OutputDebugStringA(("[VK] " + message + "\n").c_str());
}

// VK_CHECK(expr)
//   Pass any VkResult expression.
//   Returns bool — true on VK_SUCCESS, false (+ log) otherwise.
//
//   Usage:
//     if (!VK_CHECK(vkCreateFoo(...))) return false;
//     VK_CHECK(vkCreateFoo(...));   // fire-and-forget
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


// =============================================================================
//  ██████╗ ██╗      █████╗ ███████╗███████╗███████╗███████╗
//  ██╔════╝██║     ██╔══██╗██╔════╝██╔════╝██╔════╝██╔════╝
//  ██║     ██║     ███████║███████╗███████╗█████╗  ███████╗
//  ██║     ██║     ██╔══██║╚════██║╚════██║██╔══╝  ╚════██║
//  ╚██████╗███████╗██║  ██║███████║███████║███████╗███████║
//   ╚═════╝╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝╚══════╝
// =============================================================================

namespace VulkanBaseplate {

// -----------------------------------------------------------------------------
//  VulkanContext.h
//
//  Owns:
//    • VkInstance
//    • VkDebugUtilsMessengerEXT   (debug builds only)
//    • VkSurfaceKHR               (Win32 surface)
//
//  Does NOT own: physical device, logical device, swapchain.
// -----------------------------------------------------------------------------
class VulkanContext
{
public:
    VulkanContext()  = default;
    ~VulkanContext() = default;

    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Initialize(HWND windowHandle, const std::string& appName);  // instance + debug messenger + surface
    void Shutdown();                                                  // surface → debug messenger → instance

    // ── Accessors ─────────────────────────────────────────────────────────────
    VkInstance   GetInstance()  const { return Instance; }
    VkSurfaceKHR GetSurface()   const { return Surface; }

    const std::vector<const char*>& GetEnabledExtensions() const { return EnabledExtensions; }
    bool IsValidationEnabled() const { return ValidationEnabled; }

private:
    // ── Internal helpers (implemented in VulkanContext.cpp) ───────────────────
    bool CreateInstance(const std::string& appName);
    bool CreateDebugMessenger();
    bool CreateSurface(HWND windowHandle);
    bool CheckValidationLayerSupport();
    std::vector<const char*> BuildRequiredExtensions();

    // Static debug callback — matches PFN_vkDebugUtilsMessengerCallbackEXT
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT             messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       pUserData);

    // Dynamic loader (not in static Vulkan lib — resolved via vkGetInstanceProcAddr)
    PFN_vkCreateDebugUtilsMessengerEXT  fnCreateDebugMessenger  = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT fnDestroyDebugMessenger = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    VkInstance               Instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             Surface        = VK_NULL_HANDLE;

    std::vector<const char*> EnabledExtensions;
    bool                     ValidationEnabled = false;

    static constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
};


// -----------------------------------------------------------------------------
//  VulkanDevice.h
//
//  Responsibilities:
//    1. Enumerate and score physical devices (prefer discrete GPU).
//    2. Discover queue families (graphics + present).
//    3. Create VkDevice with required extensions (VK_KHR_swapchain).
//    4. Retrieve graphics and present queue handles.
//    5. Create and own the VMA allocator.
// -----------------------------------------------------------------------------

struct QueueFamilyIndices
{
    std::optional<uint32_t> GraphicsFamily;
    std::optional<uint32_t> PresentFamily;

    bool IsComplete()  const { return GraphicsFamily.has_value() && PresentFamily.has_value(); }
    bool IsCombined()  const { return IsComplete() && (GraphicsFamily.value() == PresentFamily.value()); }
};

class VulkanDevice
{
public:
    VulkanDevice()  = default;
    ~VulkanDevice() = default;

    VulkanDevice(const VulkanDevice&)            = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Initialize(VkInstance instance, VkSurfaceKHR surface);
    void Shutdown();

    // ── Core accessors ────────────────────────────────────────────────────────
    VkDevice           GetDevice()           const { return m_LogicalDevice; }
    VkPhysicalDevice   GetPhysicalDevice()   const { return m_PhysicalDevice; }
    VkQueue            GetGraphicsQueue()    const { return m_GraphicsQueue; }
    VkQueue            GetPresentQueue()     const { return m_PresentQueue; }
    VmaAllocator       GetAllocator()        const { return m_Allocator; }

    const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

    // ── Swapchain support (queried by VulkanSwapchain) ────────────────────────
    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR        Capabilities{};
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR>   PresentModes;
    };

    SwapchainSupportDetails QuerySwapchainSupport(VkSurfaceKHR surface) const;

private:
    // ── Internal helpers (implemented in VulkanDevice.cpp) ────────────────────
    bool PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    int  ScorePhysicalDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    bool IsPhysicalDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    bool CreateLogicalDevice();
    bool CreateAllocator(VkInstance instance);

    // ── State ─────────────────────────────────────────────────────────────────
    VkPhysicalDevice   m_PhysicalDevice     = VK_NULL_HANDLE;
    VkDevice           m_LogicalDevice      = VK_NULL_HANDLE;
    VkQueue            m_GraphicsQueue      = VK_NULL_HANDLE;
    VkQueue            m_PresentQueue       = VK_NULL_HANDLE;
    VmaAllocator       m_Allocator          = VK_NULL_HANDLE;
    QueueFamilyIndices m_QueueFamilyIndices;

    static constexpr const char* k_RequiredDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};


// -----------------------------------------------------------------------------
//  VulkanSwapchain.h
//
//  Responsibilities:
//    1. Choose surface format, present mode, and extent.
//    2. Create VkSwapchainKHR (3 images, correct sharing mode).
//    3. Retrieve swapchain images and create VkImageViews.
//    4. Recreate cleanly on WM_SIZE (destroy old → build new).
// -----------------------------------------------------------------------------
class VulkanSwapchain
{
public:
    VulkanSwapchain()  = default;
    ~VulkanSwapchain() = default;

    VulkanSwapchain(const VulkanSwapchain&)            = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Initialize(VulkanDevice& device, VkSurfaceKHR surface, uint32_t width, uint32_t height);
    void Shutdown();

    // Call from WM_SIZE handler after vkDeviceWaitIdle
    bool Recreate(uint32_t width, uint32_t height);

    // ── Accessors ─────────────────────────────────────────────────────────────
    VkSwapchainKHR                   GetSwapchain()   const { return m_Swapchain; }
    VkFormat                         GetImageFormat() const { return m_ImageFormat; }
    VkExtent2D                       GetExtent()      const { return m_Extent; }
    uint32_t                         GetImageCount()  const { return static_cast<uint32_t>(m_Images.size()); }
    const std::vector<VkImage>&      GetImages()      const { return m_Images; }
    const std::vector<VkImageView>&  GetImageViews()  const { return m_ImageViews; }

private:
    // ── Internal helpers (implemented in VulkanSwapchain.cpp) ─────────────────
    bool CreateSwapchain(uint32_t width, uint32_t height);
    void DestroySwapchainResources();
    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) const;
    VkPresentModeKHR   ChoosePresentMode(const std::vector<VkPresentModeKHR>& available) const;
    VkExtent2D         ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                    uint32_t width, uint32_t height) const;

    // ── References (not owned) ────────────────────────────────────────────────
    VulkanDevice* m_Device  = nullptr;
    VkSurfaceKHR  m_Surface = VK_NULL_HANDLE;

    // ── State ─────────────────────────────────────────────────────────────────
    VkSwapchainKHR           m_Swapchain   = VK_NULL_HANDLE;
    VkFormat                 m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_Extent      = { 0, 0 };

    std::vector<VkImage>     m_Images;
    std::vector<VkImageView> m_ImageViews;
};


// -----------------------------------------------------------------------------
//  VulkanBuffer.h
//
//  Generic VkBuffer + VmaAllocation wrapper.
//
//  GPU-only upload workflow:
//    1. staging.CreateStaging(device, size)
//    2. staging.Upload(data, size)
//    3. vbo.CreateVertex(device, size)   or   ibo.CreateIndex(device, size)
//    4. staging.RecordCopyTo(cmd, vbo, size)     ← inside open command buffer
//    5. Submit cmd, vkQueueWaitIdle, staging.Shutdown()
// -----------------------------------------------------------------------------
class VulkanBuffer
{
public:
    VulkanBuffer()  = default;
    ~VulkanBuffer() = default;

    VulkanBuffer(const VulkanBuffer&)            = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    // ── Generic creation ──────────────────────────────────────────────────────
    bool Create(VulkanDevice&      device,
                VkDeviceSize       size,
                VkBufferUsageFlags usageFlags,
                VmaMemoryUsage     memoryUsage);

    void Shutdown();

    // ── Named helpers ─────────────────────────────────────────────────────────
    bool CreateStaging(VulkanDevice& device, VkDeviceSize size);   // CPU-visible TRANSFER_SRC
    bool CreateVertex (VulkanDevice& device, VkDeviceSize size);   // GPU-only   VERTEX_BUFFER | TRANSFER_DST
    bool CreateIndex  (VulkanDevice& device, VkDeviceSize size);   // GPU-only   INDEX_BUFFER  | TRANSFER_DST
    bool CreateUniform(VulkanDevice& device, VkDeviceSize size);   // CPU→GPU    UNIFORM_BUFFER, persistently mapped

    // ── Data upload (host-visible buffers only) ───────────────────────────────
    // Maps, memcpy, unmaps. NOT valid on GPU_ONLY buffers.
    bool Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    // ── Command recording ─────────────────────────────────────────────────────
    // Records a buffer→buffer copy into an already-open command buffer.
    void RecordCopyTo(VkCommandBuffer cmd,
                      VulkanBuffer&   destination,
                      VkDeviceSize    size,
                      VkDeviceSize    srcOffset = 0,
                      VkDeviceSize    dstOffset = 0) const;

    // ── Accessors ─────────────────────────────────────────────────────────────
    VkBuffer     GetBuffer() const { return m_Buffer; }
    VkDeviceSize GetSize()   const { return m_Size; }
    bool         IsValid()   const { return m_Buffer != VK_NULL_HANDLE; }

private:
    VulkanDevice* m_Device     = nullptr;
    VkBuffer      m_Buffer     = VK_NULL_HANDLE;
    VmaAllocation m_Allocation = VK_NULL_HANDLE;
    VkDeviceSize  m_Size       = 0;
};


// -----------------------------------------------------------------------------
//  VulkanImage.h
//
//  Wraps VkImage + VkImageView + VmaAllocation.
//  All images: TILING_OPTIMAL (device-local).
//
//  Texture upload workflow:
//    1. image.Create(device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
//                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
//                   VK_IMAGE_ASPECT_COLOR_BIT)
//    2. image.RecordLayoutTransition(cmd,
//                                    VK_IMAGE_LAYOUT_UNDEFINED,
//                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
//    3. image.RecordCopyFromBuffer(cmd, stagingBuffer, width, height)
//    4. image.RecordLayoutTransition(cmd,
//                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
//    5. Submit cmd, vkQueueWaitIdle, staging.Shutdown()
//
//  Depth images: use VK_IMAGE_ASPECT_DEPTH_BIT + VK_FORMAT_D32_SFLOAT (etc.)
// -----------------------------------------------------------------------------
class VulkanImage
{
public:
    VulkanImage()  = default;
    ~VulkanImage() = default;

    VulkanImage(const VulkanImage&)            = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

    // ── Creation ──────────────────────────────────────────────────────────────
    bool Create(VulkanDevice&     device,
                uint32_t          width,
                uint32_t          height,
                VkFormat          format,
                VkImageUsageFlags usageFlags,
                VkImageAspectFlags aspectFlags);

    void Shutdown();

    // ── Command recording ─────────────────────────────────────────────────────
    // Pipeline barrier — transitions image layout. Call inside an open command buffer.
    void RecordLayoutTransition(VkCommandBuffer cmd,
                                VkImageLayout   oldLayout,
                                VkImageLayout   newLayout);

    // Buffer→image copy. Image must already be in TRANSFER_DST_OPTIMAL layout.
    void RecordCopyFromBuffer(VkCommandBuffer cmd,
                              VulkanBuffer&   sourceBuffer,
                              uint32_t        width,
                              uint32_t        height);

    // ── Accessors ─────────────────────────────────────────────────────────────
    VkImage     GetImage()     const { return m_Image; }
    VkImageView GetImageView() const { return m_ImageView; }
    VkFormat    GetFormat()    const { return m_Format; }
    bool        IsValid()      const { return m_Image != VK_NULL_HANDLE; }

private:
    VulkanDevice*      m_Device      = nullptr;
    VkImage            m_Image       = VK_NULL_HANDLE;
    VkImageView        m_ImageView   = VK_NULL_HANDLE;
    VmaAllocation      m_Allocation  = VK_NULL_HANDLE;
    VkFormat           m_Format      = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags m_AspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
};


// -----------------------------------------------------------------------------
//  VulkanPipeline.h
//
//  Owns: VkRenderPass + VkPipelineLayout + VkPipeline (single graphics pass).
//
//  Fixed pipeline state:
//    Topology         TRIANGLE_LIST
//    Polygon          FILL, back-face cull, CCW front face
//    Blend            alpha-blend (src_alpha / one_minus_src_alpha)
//    Depth            disabled
//    Viewport/Scissor dynamic  →  call vkCmdSetViewport/Scissor every frame
//
//  Render pass:
//    Single colour attachment matching swapchainFormat.
//    loadOp=CLEAR, storeOp=STORE
//    initial=UNDEFINED, final=PRESENT_SRC_KHR
// -----------------------------------------------------------------------------
class VulkanPipeline
{
public:
    // ── Input descriptors ──────────────────────────────────────────────────────
    struct ShaderInfo
    {
        std::vector<uint32_t> VertexSpirv;
        std::vector<uint32_t> FragmentSpirv;
    };

    struct VertexInputInfo
    {
        std::vector<VkVertexInputBindingDescription>   Bindings;
        std::vector<VkVertexInputAttributeDescription> Attributes;
    };

    // ──────────────────────────────────────────────────────────────────────────
    VulkanPipeline()  = default;
    ~VulkanPipeline() = default;

    VulkanPipeline(const VulkanPipeline&)            = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Initialize(VulkanDevice&        device,
                    VkFormat             swapchainFormat,
                    const ShaderInfo&    shaders,
                    const VertexInputInfo& vertexInput);

    void Shutdown();

    // ── Accessors ─────────────────────────────────────────────────────────────
    VkPipeline       GetPipeline()       const { return m_Pipeline; }
    VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
    VkRenderPass     GetRenderPass()     const { return m_RenderPass; }

private:
    // ── Internal helpers (implemented in VulkanPipeline.cpp) ──────────────────
    bool CreateRenderPass(VkFormat swapchainFormat);
    bool CreatePipelineLayout();
    bool CreateGraphicsPipeline(const ShaderInfo& shaders, const VertexInputInfo& vertexInput);
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);

    // ── State ─────────────────────────────────────────────────────────────────
    VulkanDevice*    m_Device         = nullptr;
    VkRenderPass     m_RenderPass     = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_Pipeline       = VK_NULL_HANDLE;
};


// -----------------------------------------------------------------------------
//  VulkanSync.h
//
//  Per-frame synchronisation primitives (MAX_FRAMES_IN_FLIGHT slots).
//
//  Per slot:
//    imageAvailableSemaphore  — fired by vkAcquireNextImageKHR
//    renderFinishedSemaphore  — fired when the submitted command buffer finishes
//    inFlightFence            — CPU stalls here before reusing the frame slot
//
//  All fences created pre-signalled so frame 0 never blocks.
// -----------------------------------------------------------------------------

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

class VulkanSync
{
public:
    VulkanSync()  = default;
    ~VulkanSync() = default;

    VulkanSync(const VulkanSync&)            = delete;
    VulkanSync& operator=(const VulkanSync&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Initialize(VulkanDevice& device);
    void Shutdown();

    // ── Per-frame accessors ───────────────────────────────────────────────────
    VkSemaphore GetImageAvailableSemaphore(uint32_t frameIndex) const { return m_ImageAvailableSemaphores[frameIndex]; }
    VkSemaphore GetRenderFinishedSemaphore(uint32_t frameIndex) const { return m_RenderFinishedSemaphores[frameIndex]; }
    VkFence     GetInFlightFence          (uint32_t frameIndex) const { return m_InFlightFences[frameIndex]; }

    // ── Frame counter ─────────────────────────────────────────────────────────
    uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
    void     AdvanceFrame()               { m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

private:
    VulkanDevice* m_Device = nullptr;

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_ImageAvailableSemaphores{};
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_RenderFinishedSemaphores{};
    std::array<VkFence,     MAX_FRAMES_IN_FLIGHT> m_InFlightFences{};

    uint32_t m_CurrentFrame = 0;
};


// -----------------------------------------------------------------------------
//  VulkanCommand.h
//
//  Owns: VkCommandPool + one primary VkCommandBuffer per frame slot.
//  Pool flag: RESET_COMMAND_BUFFER_BIT (individual buffer reset, not pool reset).
//
//  Per-frame usage:
//    BeginRecording(frameIndex)   — vkResetCommandBuffer + vkBeginCommandBuffer
//    < record vkCmd* calls >
//    EndRecording(frameIndex)     — vkEndCommandBuffer
//    < submit buffer via VkQueue >
// -----------------------------------------------------------------------------
class VulkanCommand
{
public:
    VulkanCommand()  = default;
    ~VulkanCommand() = default;

    VulkanCommand(const VulkanCommand&)            = delete;
    VulkanCommand& operator=(const VulkanCommand&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Initialize(VulkanDevice& device);
    void Shutdown();

    // ── Per-frame recording ───────────────────────────────────────────────────
    bool BeginRecording(uint32_t frameIndex);
    bool EndRecording  (uint32_t frameIndex);

    // ── Accessors ─────────────────────────────────────────────────────────────
    VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_CommandBuffers[frameIndex]; }
    VkCommandPool   GetCommandPool()                      const { return m_CommandPool; }

private:
    VulkanDevice* m_Device = nullptr;

    VkCommandPool                                     m_CommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_CommandBuffers{};
};


} // namespace VulkanBaseplate



// =============================================================================
//  ███████╗██╗  ██╗██████╗  █████╗ ███╗   ██╗███████╗██╗ ██████╗ ███╗   ██╗
//  ██╔════╝╚██╗██╔╝██╔══██╗██╔══██╗████╗  ██║██╔════╝██║██╔═══██╗████╗  ██║
//  █████╗   ╚███╔╝ ██████╔╝███████║██╔██╗ ██║███████╗██║██║   ██║██╔██╗ ██║
//  ██╔══╝   ██╔██╗ ██╔═══╝ ██╔══██║██║╚██╗██║╚════██║██║██║   ██║██║╚██╗██║
//  ███████╗██╔╝ ██╗██║     ██║  ██║██║ ╚████║███████║██║╚██████╔╝██║ ╚████║
//  ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝
// =============================================================================
//  VulkanModuleExpansion  —  arm extension layer for VulkanModule
//
//  Declared in VulkanModuleExpansion.h (auto-included below).
//  Implemented in VulkanModuleExpansion.cpp.
//  All classes live in namespace VulkanBaseplate.
//
//  DESIGN RULE
//  ───────────
//  Every expansion class receives the six base objects by reference or raw
//  pointer.  They do NOT own, construct, or destroy any base object.
//  The original init/shutdown order is completely unchanged:
//
//    Init:      Context → Device → Swapchain → Pipeline → Command → Sync
//    Shutdown:  Sync → Command → Pipeline → Swapchain → Device → Context
//
//  All expansion objects must be Shutdown() BEFORE the base objects they
//  reference.  A typical ordering is shown in the function index below.
//
//  SOURCE FILES ADDED
//  ──────────────────
//  Header         : VulkanModuleExpansion.h
//  Implementation : VulkanModuleExpansion.cpp
//
//  CLASSES  (12)
//  ─────────────
//  [1]  VulkanOneTimeCommand          — one-shot GPU command using the existing pool
//  [2]  VulkanFramebufferSet          — per-swapchain-image VkFramebuffers
//  [3]  VulkanDepthBuffer             — depth/stencil image wrapping VulkanImage
//  [4]  VulkanSampler                 — VkSampler with nearest / linear factory methods
//  [5]  VulkanTexture                 — VulkanImage + VulkanSampler, hidden staging upload
//  [6]  VulkanMesh                    — vertex + index VulkanBuffers with RecordDraw()
//  [7]  VulkanDescriptorLayoutBuilder — fluent VkDescriptorSetLayout builder
//  [8]  VulkanDescriptorPool          — VkDescriptorPool + per-frame set allocation
//  [9]  VulkanUniformSet<T>           — per-frame typed UBO with Write() + GetSet()
//  [10] VulkanDescriptorAllocator     — general-purpose pool supporting multiple descriptor types
//  [11] VulkanModelPipeline           — full model pipeline with UBO layouts + push constants
//  [12] VulkanMipmapGenerator         — blit-based mip chain generation for any VkImage
//
//  HELPER FUNCTION (file-static, internal)
//  ───────────────────────────────────────
//  FindDepthFormat(VulkanDevice&)
//    Iterates D32_SFLOAT → D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT and
//    returns the first format that supports DEPTH_STENCIL_ATTACHMENT optimal
//    tiling.  Called internally by VulkanDepthBuffer::Initialize() and Recreate().
// =============================================================================


// -----------------------------------------------------------------------------
//  VulkanOneTimeCommand
//
//  Allocates a single transient VkCommandBuffer from the existing
//  VulkanCommand pool, records your work, submits it to the graphics queue,
//  and blocks (vkQueueWaitIdle) until the GPU is done.
//  Frees the command buffer on End().
//
//  Use for one-off GPU operations: staging copies, image layout transitions,
//  mipmap generation.  Not intended for per-frame use.
//
//  Usage:
//    VulkanOneTimeCommand otc;
//    if (otc.Begin(device, command))
//    {
//        // record vkCmd* calls via otc.Cmd()
//        otc.End();   // submit + vkQueueWaitIdle + free
//    }
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool            Begin(VulkanDevice& device, VulkanCommand& command)
//    Allocates from command.GetCommandPool() and opens the buffer.
//    Returns false if allocation or vkBeginCommandBuffer fails.
//  void            End()
//    Ends recording, submits to device.GetGraphicsQueue(),
//    calls vkQueueWaitIdle, then frees the buffer back to the pool.
//
//  ── Accessor ──────────────────────────────────────────────────────────────
//  VkCommandBuffer Cmd() const
//    Returns the open command buffer.  Valid between Begin() and End().
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanFramebufferSet
//
//  Creates one VkFramebuffer per swapchain image and stores them in a vector.
//  Optionally attaches a depth image view as the second framebuffer attachment.
//
//  Typically created after VulkanPipeline and recreated on every resize.
//  Pass Get(imageIndex) as the framebuffer in VkRenderPassBeginInfo.
//
//  Usage:
//    VulkanFramebufferSet fbs;
//    fbs.Initialize(device, swapchain, pipeline.GetRenderPass(),
//                   depth.GetImageView());   // omit last arg for colour-only
//
//    // on resize (after swapchain.Recreate() + depth.Recreate()):
//    fbs.Recreate(pipeline.GetRenderPass(), depth.GetImageView());
//
//    // per frame:
//    beginInfo.framebuffer = fbs.Get(frame.ImageIndex);
//
//    fbs.Shutdown();   // before pipeline.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&    device,
//                  VulkanSwapchain& swapchain,
//                  VkRenderPass     renderPass,
//                  VkImageView      depthView = VK_NULL_HANDLE)
//    Creates one framebuffer per swapchain image view.
//    Pass VK_NULL_HANDLE for depthView on colour-only render passes.
//  void Shutdown()
//    Destroys all framebuffers and releases references.
//  bool Recreate(VkRenderPass renderPass,
//                VkImageView  depthView = VK_NULL_HANDLE)
//    Destroys existing framebuffers and rebuilds from the current swapchain
//    image views.  Call after VulkanSwapchain::Recreate().
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkFramebuffer Get(uint32_t imageIndex) const
//    Returns the framebuffer for the given swapchain image index.
//  uint32_t      Count() const
//    Number of framebuffers (equals swapchain image count).
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDepthBuffer
//
//  Depth/stencil image backed by VulkanImage.  The best available format is
//  chosen automatically by FindDepthFormat():
//    D32_SFLOAT  →  D32_SFLOAT_S8_UINT  →  D24_UNORM_S8_UINT
//
//  The image is created and immediately transitioned to
//  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL on Initialize().
//
//  Usage:
//    VulkanDepthBuffer depth;
//    depth.Initialize(device, width, height);
//
//    // pass to VulkanFramebufferSet:
//    fbs.Initialize(device, swapchain, renderPass, depth.GetImageView());
//
//    // on resize (after swapchain.Recreate()):
//    depth.Recreate(newWidth, newHeight);
//
//    depth.Shutdown();   // before device.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice& device, uint32_t width, uint32_t height)
//  void Shutdown()
//  bool Recreate(uint32_t width, uint32_t height)
//    Calls Shutdown() + Initialize() in one step.  device reference is kept
//    from the original Initialize() call.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkImageView GetImageView() const
//  VkFormat    GetFormat()    const
//  bool        IsValid()      const
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanSampler
//
//  Thin wrapper around VkSampler with two filter-mode factory methods.
//  Anisotropy is queried from the physical device and enabled automatically
//  when the device supports it.
//
//  Usage:
//    VulkanSampler sampler;
//    sampler.CreateLinear(device);
//
//    // in a descriptor write:
//    imageInfo.sampler = sampler.GetSampler();
//
//    sampler.Shutdown();   // before device.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool CreateNearest(VulkanDevice& device)
//    VK_FILTER_NEAREST, no mipmapping.
//    Best for pixel-perfect / UI textures.
//  bool CreateLinear(VulkanDevice& device)
//    VK_FILTER_LINEAR, no mipmapping.
//    General-purpose smooth textures.
//  void Shutdown()
//    Destroys the VkSampler.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkSampler GetSampler() const
//  bool      IsValid()    const
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanTexture
//
//  One-stop GPU texture: VulkanImage (R8G8B8A8_SRGB) + VulkanSampler (linear).
//  CreateFromPixels() performs the complete CPU→GPU upload without any manual
//  staging management:
//    1. Allocates a staging VulkanBuffer.
//    2. Copies pixels into it.
//    3. Opens a VulkanOneTimeCommand.
//    4. Transitions image UNDEFINED → TRANSFER_DST_OPTIMAL.
//    5. Copies buffer → image.
//    6. Transitions image TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
//    7. Submits, waits, frees the staging buffer.
//
//  Usage:
//    VulkanTexture tex;
//    tex.CreateFromPixels(device, command, pixels, 512, 512);
//
//    // in a descriptor write:
//    imageInfo.imageView   = tex.GetImageView();
//    imageInfo.sampler     = tex.GetSampler();
//    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//
//    tex.Shutdown();   // before device.Shutdown()
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool CreateFromPixels(VulkanDevice&  device,
//                        VulkanCommand& command,
//                        const uint8_t* pixels,
//                        uint32_t       width,
//                        uint32_t       height)
//    pixels must point to width * height * 4 bytes (RGBA8).
//  void Shutdown()
//    Destroys the image and sampler.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkImageView GetImageView() const
//  VkSampler   GetSampler()   const
//  bool        IsValid()      const
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanMesh
//
//  GPU-resident vertex buffer and optional index buffer.
//  Upload is done once via a VulkanOneTimeCommand + staging buffers.
//  Staging memory is freed immediately after the GPU copy completes.
//  Call RecordDraw() every frame inside an active render pass.
//
//  Usage:
//    VulkanMesh mesh;
//    mesh.Upload(device, command,
//                verts.data(), sizeof(verts),
//                indices.data(), (uint32_t)indices.size());
//
//    // non-indexed:
//    mesh.Upload(device, command, verts.data(), sizeof(verts), nullptr, 0);
//
//    // inside render pass, per frame:
//    mesh.RecordDraw(cmd);
//
//    mesh.Shutdown();   // before device.Shutdown()
//
//  RecordDraw() will:
//    • vkCmdBindVertexBuffers  — binding 0
//    • vkCmdBindIndexBuffer    — only when an index buffer was uploaded
//    • vkCmdDrawIndexed / vkCmdDraw
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Upload(VulkanDevice&   device,
//              VulkanCommand&  command,
//              const void*     vertices,    // raw vertex data
//              VkDeviceSize    vertexSize,  // total byte size of vertex data
//              const uint32_t* indices,     // uint32 index array (or nullptr)
//              uint32_t        indexCount)  // 0 for non-indexed
//  void Shutdown()
//
//  ── Per-frame recording ───────────────────────────────────────────────────
//  void RecordDraw(VkCommandBuffer cmd) const
//    Binds buffers and issues one draw call.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  bool IsValid()    const   — true if the vertex buffer was uploaded successfully
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDescriptorLayoutBuilder
//
//  Fluent builder for VkDescriptorSetLayout.
//  Chain Add() calls in binding-slot order, then call Build().
//  The returned layout is caller-owned — destroy it with
//  vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr) when done.
//
//  Usage:
//    VkDescriptorSetLayout layout =
//        VulkanDescriptorLayoutBuilder{}
//            .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                    VK_SHADER_STAGE_VERTEX_BIT)
//            .Add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//                    VK_SHADER_STAGE_FRAGMENT_BIT)
//            .Build(device);
//
//    // …use layout…
//
//    vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr);
//
//  ── Builder methods ───────────────────────────────────────────────────────
//  VulkanDescriptorLayoutBuilder& Add(uint32_t           binding,
//                                     VkDescriptorType   type,
//                                     VkShaderStageFlags stages)
//    Appends one binding and returns *this for chaining.
//  VkDescriptorSetLayout Build(VulkanDevice& device) const
//    Creates and returns the layout.  Returns VK_NULL_HANDLE on failure.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDescriptorPool
//
//  Creates a VkDescriptorPool pre-sized for one descriptor type and
//  pre-allocates exactly MAX_FRAMES_IN_FLIGHT (2) descriptor sets from a
//  given layout in a single Initialize() call — the standard pattern for
//  per-frame uniform data.
//
//  Usage:
//    VkDescriptorSetLayout layout =
//        VulkanDescriptorLayoutBuilder{}
//            .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                    VK_SHADER_STAGE_VERTEX_BIT)
//            .Build(device);
//
//    VulkanDescriptorPool pool;
//    pool.Initialize(device, layout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
//
//    // per frame:
//    VkDescriptorSet set = pool.GetSet(frameIndex);
//
//    pool.Shutdown();
//    vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr);
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&         device,
//                  VkDescriptorSetLayout layout,
//                  VkDescriptorType      type)
//    Creates the pool and allocates MAX_FRAMES_IN_FLIGHT sets from layout.
//  void Shutdown()
//    Destroys the pool (all allocated sets are implicitly freed).
//
//  ── Accessor ──────────────────────────────────────────────────────────────
//  VkDescriptorSet GetSet(uint32_t frameIndex) const
//    Returns the pre-allocated descriptor set for the given frame slot.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanUniformSet<T>
//
//  Template wrapper: one per-frame uniform buffer of type T, wired to the
//  descriptor sets provided by VulkanDescriptorPool.
//  T must be a trivially-copyable struct (matrices, light params, etc.).
//
//  Initialize() creates MAX_FRAMES_IN_FLIGHT (2) VulkanBuffer::CreateUniform
//  buffers and calls vkUpdateDescriptorSets to point each frame's descriptor
//  set at its matching buffer.
//  Write() does a plain memcpy into the persistently-mapped buffer.
//
//  Usage:
//    struct FrameUBO { float viewProj[16]; float camPos[4]; };
//
//    VkDescriptorSetLayout layout =
//        VulkanDescriptorLayoutBuilder{}
//            .Add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                    VK_SHADER_STAGE_VERTEX_BIT)
//            .Build(device);
//
//    VulkanDescriptorPool pool;
//    pool.Initialize(device, layout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
//
//    VulkanUniformSet<FrameUBO> ubo;
//    ubo.Initialize(device, pool, 0 /* binding */);
//
//    // every frame:
//    ubo.Write(frameIndex, frameData);
//    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
//                            pipelineLayout, 0, 1,
//                            &ubo.GetSet(frameIndex), 0, nullptr);
//
//    ubo.Shutdown();   // before pool.Shutdown()
//    pool.Shutdown();
//    vkDestroyDescriptorSetLayout(device.GetDevice(), layout, nullptr);
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&        device,
//                  VulkanDescriptorPool& pool,
//                  uint32_t              binding)
//    Allocates per-frame UBO buffers and writes descriptor updates.
//  void Shutdown()
//    Calls VulkanBuffer::Shutdown() on all per-frame buffers.
//
//  ── Per-frame use ─────────────────────────────────────────────────────────
//  void            Write(uint32_t frameIndex, const T& data)
//    memcpy T into the frame slot's mapped uniform buffer.
//  VkDescriptorSet GetSet(uint32_t frameIndex) const
//    Returns the descriptor set for binding via vkCmdBindDescriptorSets.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanDescriptorAllocator
//
//  General-purpose VkDescriptorPool that supports multiple descriptor types in
//  a single pool and exposes Allocate() to pull individual VkDescriptorSets
//  from any compatible layout.  All sets are freed implicitly when Shutdown()
//  destroys the pool.
//
//  Unlike VulkanDescriptorPool (which pre-allocates a fixed number of sets of
//  one type), this class is suitable for mixed layouts — e.g. a set-0 UBO
//  layout and a set-1 sampler layout allocated from the same pool.
//
//  Usage:
//    VulkanDescriptorAllocator alloc;
//    alloc.Initialize(device, 8,
//        {{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         4 },
//         { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }});
//
//    VkDescriptorSet s0 = alloc.Allocate(set0Layout);
//    VkDescriptorSet s1 = alloc.Allocate(set1Layout);
//    // ...use sets...
//    alloc.Shutdown();   // frees pool + all sets implicitly
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&                   device,
//                  uint32_t                        maxSets,
//                  initializer_list<PoolSize>      sizes)
//    Creates the pool.  maxSets is the total set count across all Allocate()
//    calls.  sizes lists each descriptor type and its total count in the pool.
//  void Shutdown()
//    Destroys the pool (all allocated sets freed implicitly).
//
//  ── Allocation ────────────────────────────────────────────────────────────
//  VkDescriptorSet Allocate(VkDescriptorSetLayout layout)
//    Allocates one descriptor set.  Returns VK_NULL_HANDLE on failure.
//
//  ── Nested type ───────────────────────────────────────────────────────────
//  struct PoolSize { VkDescriptorType type; uint32_t count; }
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//  VulkanModelPipeline
//
//  A fully-wired model pipeline.  VulkanPipeline owns the VkRenderPass and
//  creates a baseline VkPipeline with an empty layout.  VulkanModelPipeline
//  takes that render pass and builds the properly-wired VkPipeline that App
//  actually draws with.
//
//  Descriptor layout:
//    set 0, binding 0 — per-frame UBO          (VK_SHADER_STAGE_VERTEX_BIT)
//    set 1, binding 0 — combined image/sampler (VK_SHADER_STAGE_FRAGMENT_BIT)
//
//  Push constant (VK_SHADER_STAGE_VERTEX_BIT, 64 bytes):
//    mat4 model
//
//  Usage (after VulkanPipeline::Initialize has run):
//    VulkanModelPipeline mp;
//    mp.Initialize(device, pipeline.GetRenderPass(), shaders, vertexInput);
//
//    // per frame:
//    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mp.GetPipeline());
//    vkCmdBindDescriptorSets(cmd, ..., mp.GetPipelineLayout(), ...);
//    vkCmdPushConstants(cmd, mp.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT,
//                       0, 64, &pc);
//    mesh.RecordDraw(cmd);
//
//    // shutdown before VulkanPipeline:
//    mp.Shutdown();
//
//  ── Lifecycle ─────────────────────────────────────────────────────────────
//  bool Initialize(VulkanDevice&                          device,
//                  VkRenderPass                           renderPass,
//                  const VulkanPipeline::ShaderInfo&      shaders,
//                  const VulkanPipeline::VertexInputInfo& vertexInput)
//    renderPass is borrowed from VulkanPipeline::GetRenderPass() — NOT owned.
//  void Shutdown()
//    Destroys pipeline, pipeline layout, and both descriptor set layouts.
//    Must be called before the VulkanPipeline that provided the render pass.
//
//  ── Accessors ─────────────────────────────────────────────────────────────
//  VkPipeline            GetPipeline()       const
//  VkPipelineLayout      GetPipelineLayout() const
//  VkDescriptorSetLayout GetSet0Layout()     const   — use with VulkanDescriptorAllocator
//  VkDescriptorSetLayout GetSet1Layout()     const   — use with VulkanDescriptorAllocator
// -----------------------------------------------------------------------------


// =============================================================================
//  IMPLEMENTATION FUNCTION INDEX
//  All functions defined across the .cpp files, grouped by translation unit.
//  (Declarations above; implementations in their respective .cpp files.)
// =============================================================================

/*
────────────────────────────────────────────────────────────────────────────────
 VmaImpl.cpp
   (VMA implementation unit — define VMA_IMPLEMENTATION before including
    vk_mem_alloc.h in exactly one .cpp file)
────────────────────────────────────────────────────────────────────────────────

────────────────────────────────────────────────────────────────────────────────
 VulkanHelpers.cpp
   (currently empty — LogVk and VK_CHECK are header-inline)
────────────────────────────────────────────────────────────────────────────────

────────────────────────────────────────────────────────────────────────────────
 VulkanContext.cpp
────────────────────────────────────────────────────────────────────────────────
 bool        VulkanContext::Initialize(HWND windowHandle, const std::string& appName)
 void        VulkanContext::Shutdown()
 bool        VulkanContext::CreateInstance(const std::string& appName)
 bool        VulkanContext::CreateDebugMessenger()
 bool        VulkanContext::CreateSurface(HWND windowHandle)
 bool        VulkanContext::CheckValidationLayerSupport()
 vector<const char*> VulkanContext::BuildRequiredExtensions()
 VkBool32    VulkanContext::DebugCallback(severity, type, pCallbackData, pUserData)

────────────────────────────────────────────────────────────────────────────────
 VulkanDevice.cpp
────────────────────────────────────────────────────────────────────────────────
 bool                VulkanDevice::Initialize(VkInstance, VkSurfaceKHR)
 void                VulkanDevice::Shutdown()
 bool                VulkanDevice::PickPhysicalDevice(VkInstance, VkSurfaceKHR)
 int                 VulkanDevice::ScorePhysicalDevice(VkPhysicalDevice, VkSurfaceKHR) const
 bool                VulkanDevice::IsPhysicalDeviceSuitable(VkPhysicalDevice, VkSurfaceKHR) const
 bool                VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice) const
 QueueFamilyIndices  VulkanDevice::FindQueueFamilies(VkPhysicalDevice, VkSurfaceKHR) const
 bool                VulkanDevice::CreateLogicalDevice()
 bool                VulkanDevice::CreateAllocator(VkInstance)
 SwapchainSupportDetails VulkanDevice::QuerySwapchainSupport(VkSurfaceKHR) const

────────────────────────────────────────────────────────────────────────────────
 VulkanSwapchain.cpp
────────────────────────────────────────────────────────────────────────────────
 bool               VulkanSwapchain::Initialize(VulkanDevice&, VkSurfaceKHR, uint32_t w, uint32_t h)
 void               VulkanSwapchain::Shutdown()
 bool               VulkanSwapchain::Recreate(uint32_t width, uint32_t height)
 bool               VulkanSwapchain::CreateSwapchain(uint32_t width, uint32_t height)
 void               VulkanSwapchain::DestroySwapchainResources()
 VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(const vector<VkSurfaceFormatKHR>&) const
 VkPresentModeKHR   VulkanSwapchain::ChoosePresentMode(const vector<VkPresentModeKHR>&) const
 VkExtent2D         VulkanSwapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR&, uint32_t w, uint32_t h) const

────────────────────────────────────────────────────────────────────────────────
 VulkanBuffer.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanBuffer::Create(VulkanDevice&, VkDeviceSize, VkBufferUsageFlags, VmaMemoryUsage)
 void VulkanBuffer::Shutdown()
 bool VulkanBuffer::CreateStaging(VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::CreateVertex (VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::CreateIndex  (VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::CreateUniform(VulkanDevice&, VkDeviceSize)
 bool VulkanBuffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset)
 void VulkanBuffer::RecordCopyTo(VkCommandBuffer, VulkanBuffer& dst, VkDeviceSize size,
                                  VkDeviceSize srcOffset, VkDeviceSize dstOffset) const

────────────────────────────────────────────────────────────────────────────────
 VulkanImage.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanImage::Create(VulkanDevice&, uint32_t w, uint32_t h, VkFormat,
                           VkImageUsageFlags, VkImageAspectFlags)
 void VulkanImage::Shutdown()
 void VulkanImage::RecordLayoutTransition(VkCommandBuffer, VkImageLayout old, VkImageLayout new)
 void VulkanImage::RecordCopyFromBuffer(VkCommandBuffer, VulkanBuffer& src, uint32_t w, uint32_t h)

────────────────────────────────────────────────────────────────────────────────
 VulkanPipeline.cpp
────────────────────────────────────────────────────────────────────────────────
 bool           VulkanPipeline::Initialize(VulkanDevice&, VkFormat, const ShaderInfo&, const VertexInputInfo&)
 void           VulkanPipeline::Shutdown()
 bool           VulkanPipeline::CreateRenderPass(VkFormat swapchainFormat)
 bool           VulkanPipeline::CreatePipelineLayout()
 bool           VulkanPipeline::CreateGraphicsPipeline(const ShaderInfo&, const VertexInputInfo&)
 VkShaderModule VulkanPipeline::CreateShaderModule(const vector<uint32_t>& spirv)

────────────────────────────────────────────────────────────────────────────────
 VulkanCommand.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanCommand::Initialize(VulkanDevice&)
 void VulkanCommand::Shutdown()
 bool VulkanCommand::BeginRecording(uint32_t frameIndex)
 bool VulkanCommand::EndRecording  (uint32_t frameIndex)

────────────────────────────────────────────────────────────────────────────────
 VulkanSync.cpp
────────────────────────────────────────────────────────────────────────────────
 bool VulkanSync::Initialize(VulkanDevice&)
 void VulkanSync::Shutdown()

────────────────────────────────────────────────────────────────────────────────
 VulkanModuleExpansion.cpp
────────────────────────────────────────────────────────────────────────────────

 RECOMMENDED EXPANSION SHUTDOWN ORDER (before base objects):
   modelPipeline.Shutdown()     VulkanModelPipeline          ← owns descriptor layouts; shut down before pipeline
   ubo.Shutdown()               VulkanUniformSet<T>
   descAllocator.Shutdown()     VulkanDescriptorAllocator    ← pool destruction frees all allocated sets implicitly
   pool.Shutdown()              VulkanDescriptorPool
   vkDestroyDescriptorSetLayout(...)
   mesh.Shutdown()              VulkanMesh
   tex.Shutdown()               VulkanTexture
   depth.Shutdown()             VulkanDepthBuffer
   fbs.Shutdown()               VulkanFramebufferSet
   ── then base objects ──

 VulkanOneTimeCommand
   bool            VulkanOneTimeCommand::Begin(VulkanDevice&, VulkanCommand&)
   void            VulkanOneTimeCommand::End()

 VulkanFramebufferSet
   bool            VulkanFramebufferSet::Initialize(VulkanDevice&, VulkanSwapchain&,
                                                     VkRenderPass, VkImageView)
   void            VulkanFramebufferSet::Shutdown()
   bool            VulkanFramebufferSet::Recreate(VkRenderPass, VkImageView)
   VkFramebuffer   VulkanFramebufferSet::Get(uint32_t imageIndex) const
   (private) bool  VulkanFramebufferSet::CreateAll(VkRenderPass, VkImageView)
   (private) void  VulkanFramebufferSet::DestroyAll()

 VulkanDepthBuffer
   bool            VulkanDepthBuffer::Initialize(VulkanDevice&, uint32_t w, uint32_t h)
   void            VulkanDepthBuffer::Shutdown()
   bool            VulkanDepthBuffer::Recreate(uint32_t w, uint32_t h)

 VulkanSampler
   bool            VulkanSampler::CreateNearest(VulkanDevice&)
   bool            VulkanSampler::CreateLinear(VulkanDevice&)
   void            VulkanSampler::Shutdown()
   (private) bool  VulkanSampler::Create(VulkanDevice&, VkFilter, VkSamplerMipmapMode)

 VulkanTexture
   bool            VulkanTexture::CreateFromPixels(VulkanDevice&, VulkanCommand&,
                                                    const uint8_t*, uint32_t w, uint32_t h)
   void            VulkanTexture::Shutdown()

 VulkanMesh
   bool            VulkanMesh::Upload(VulkanDevice&, VulkanCommand&,
                                       const void* vertices, VkDeviceSize vertexSize,
                                       const uint32_t* indices, uint32_t indexCount)
   void            VulkanMesh::Shutdown()
   void            VulkanMesh::RecordDraw(VkCommandBuffer) const

 VulkanDescriptorLayoutBuilder
   VulkanDescriptorLayoutBuilder& VulkanDescriptorLayoutBuilder::Add(
                                      uint32_t, VkDescriptorType, VkShaderStageFlags)
   VkDescriptorSetLayout          VulkanDescriptorLayoutBuilder::Build(VulkanDevice&) const

 VulkanDescriptorPool
   bool            VulkanDescriptorPool::Initialize(VulkanDevice&,
                                                     VkDescriptorSetLayout,
                                                     VkDescriptorType)
   void            VulkanDescriptorPool::Shutdown()

 VulkanUniformSet<T>   (header-inline template)
   bool            VulkanUniformSet<T>::Initialize(VulkanDevice&,
                                                    VulkanDescriptorPool&,
                                                    uint32_t binding)
   void            VulkanUniformSet<T>::Shutdown()
   void            VulkanUniformSet<T>::Write(uint32_t frameIndex, const T& data)
   VkDescriptorSet VulkanUniformSet<T>::GetSet(uint32_t frameIndex) const

 VulkanDescriptorAllocator
   bool            VulkanDescriptorAllocator::Initialize(VulkanDevice&,
                                                          uint32_t maxSets,
                                                          initializer_list<PoolSize> sizes)
   void            VulkanDescriptorAllocator::Shutdown()
   VkDescriptorSet VulkanDescriptorAllocator::Allocate(VkDescriptorSetLayout)

 VulkanModelPipeline
   bool            VulkanModelPipeline::Initialize(VulkanDevice&, VkRenderPass,
                                                    const VulkanPipeline::ShaderInfo&,
                                                    const VulkanPipeline::VertexInputInfo&)
   void            VulkanModelPipeline::Shutdown()
   (private) bool  VulkanModelPipeline::BuildDescriptorLayouts()
   (private) bool  VulkanModelPipeline::BuildPipelineLayout()
   (private) bool  VulkanModelPipeline::BuildGraphicsPipeline(VkRenderPass,
                                                               const ShaderInfo&,
                                                               const VertexInputInfo&)
   (private) VkShaderModule VulkanModelPipeline::CreateShaderModule(const vector<uint32_t>&)

 VulkanMipmapGenerator
   static uint32_t VulkanMipmapGenerator::MipLevels(uint32_t width, uint32_t height)
   static bool     VulkanMipmapGenerator::IsFormatSupported(VulkanDevice&, VkFormat)
   bool            VulkanMipmapGenerator::Generate(VulkanDevice&, VulkanCommand&,
                                                    VkImage, uint32_t w, uint32_t h,
                                                    uint32_t mipLevels)

*/


#include "VulkanModuleExpansion.h"

// =============================================================================
//  END OF VulkanModule.h
// =============================================================================
