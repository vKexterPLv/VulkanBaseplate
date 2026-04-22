#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <array>

// =============================================================================
//  MipmapExample
//
//  Goal: show VulkanMipmapGenerator doing real work, not just a log message.
//
//  The texture is intentionally created with raw Vulkan calls rather than
//  VulkanTexture, because VulkanTexture wraps a single mip level and doesn't
//  expose the mipLevels field.  This is the correct pattern for mip-aware
//  textures — VulkanMipmapGenerator is designed to pair with this workflow:
//
//    1. Calculate mip count:   VulkanMipmapGenerator::MipLevels(w, h)
//    2. Create VkImage manually with that mipLevels value and
//       TRANSFER_SRC | TRANSFER_DST | SAMPLED usage.
//    3. Create VmaAllocation for it.
//    4. Create VkImageView spanning all mip levels.
//    5. Upload base level pixels via staging + VulkanOneTimeCommand,
//       leaving the image in TRANSFER_DST_OPTIMAL.
//    6. Call VulkanMipmapGenerator::Generate() — blits each level from the
//       previous, then transitions the whole chain to SHADER_READ_ONLY_OPTIMAL.
//    7. Create a VkSampler with LINEAR filter and a non-zero maxLod so the
//       GPU actually selects between mip levels.
//
//  The quad is two triangles covering NDC [-1,1] with UV [0,1].  The fragment
//  shader samples the texture with the mip-aware sampler.  Resize the window
//  small to observe coarser mip levels averaging the checkerboard to grey.
// =============================================================================

namespace VCK::MipmapExample {

    // ─────────────────────────────────────────────────────────────────────────
    //  Window state
    // ─────────────────────────────────────────────────────────────────────────
    std::string title         = "MipmapExample";
    GLFWwindow* window        = nullptr;
    int         window_width  = 1280;
    int         window_height = 720;

    bool g_Resized   = false;
    bool g_Minimized = false;

    // ─────────────────────────────────────────────────────────────────────────
    //  Vertex — position + UV
    // ─────────────────────────────────────────────────────────────────────────
    struct Vertex {
        float position[3];   // location 0
        float uv[2];         // location 1
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Texture constants
    // ─────────────────────────────────────────────────────────────────────────
    static constexpr uint32_t  kTexW   = 512;
    static constexpr uint32_t  kTexH   = 512;
    static constexpr uint32_t  kTile   = 128;    // small tiles — more pattern, sharper mip transitions
    static constexpr VkFormat  kFmt    = VK_FORMAT_R8G8B8A8_UNORM;  // no gamma softening

    // ─────────────────────────────────────────────────────────────────────────
    //  Vulkan objects — core
    // ─────────────────────────────────────────────────────────────────────────
    VulkanContext        context;
    VulkanDevice         device;
    VulkanSwapchain      swapchain;
    VulkanPipeline       pipeline;
    VulkanCommand        command;
    VulkanSync           sync;

    // ─────────────────────────────────────────────────────────────────────────
    //  Vulkan objects — expansion
    // ─────────────────────────────────────────────────────────────────────────
    VulkanModelPipeline       modelPipeline;
    VulkanFramebufferSet      framebuffers;
    VulkanMesh                quad;
    VulkanDescriptorAllocator descAllocator;

    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> set0Sets{};
    VkDescriptorSet                                   set1Set = VK_NULL_HANDLE;

    std::array<VulkanBuffer, MAX_FRAMES_IN_FLIGHT> uboBuffers{};

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

    // ─────────────────────────────────────────────────────────────────────────
    //  Mip-aware texture — owned manually (raw Vulkan + VMA)
    // ─────────────────────────────────────────────────────────────────────────
    VkImage       g_TexImage     = VK_NULL_HANDLE;
    VkImageView   g_TexView      = VK_NULL_HANDLE;
    VmaAllocation g_TexAlloc     = VK_NULL_HANDLE;
    VkSampler     g_TexSampler   = VK_NULL_HANDLE;
    uint32_t      g_MipLevels    = 1;

    // ─────────────────────────────────────────────────────────────────────────
    //  Per-frame UBO — just a dummy mat4 identity; the quad shader ignores it
    //  but VulkanModelPipeline::GetSet0Layout() always has binding 0.
    // ─────────────────────────────────────────────────────────────────────────
    struct alignas(16) DummyUBO { float m[16]; };

    // ─────────────────────────────────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<uint32_t> LoadSpv(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> buf(size / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buf.data()), size);
        return buf;
    }

    void HandleResize()
    {
        if (window_width == 0 || window_height == 0) return;
        vkDeviceWaitIdle(device.GetDevice());
        swapchain.Recreate(window_width, window_height);
        framebuffers.Recreate(pipeline.GetRenderPass());
    }

    void OnFramebufferResize(GLFWwindow*, int w, int h)
    {
        window_width  = w;
        window_height = h;
        if (w == 0 || h == 0) { g_Minimized = true; return; }
        g_Minimized = false;
        g_Resized   = true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  CreateMipTexture
    //
    //  Full workflow:
    //    1. Generate checkerboard pixels on the CPU.
    //    2. Create VkImage with mipLevels = VulkanMipmapGenerator::MipLevels().
    //    3. Upload base level via staging + VulkanOneTimeCommand.
    //    4. Call VulkanMipmapGenerator::Generate() to blit all remaining levels.
    //    5. Create VkImageView spanning all mip levels.
    //    6. Create a VkSampler with LINEAR filter + full mip range.
    // ─────────────────────────────────────────────────────────────────────────
    void CreateMipTexture()
    {
        // ── 1. Checkerboard pixels ────────────────────────────────────────────
        std::vector<uint8_t> pixels(kTexW * kTexH * 4);
        for (uint32_t y = 0; y < kTexH; y++) {
            for (uint32_t x = 0; x < kTexW; x++) {
                bool     white = ((x / kTile) + (y / kTile)) % 2 == 0;
                uint8_t  v     = white ? 255 : 0;
                uint32_t idx   = (y * kTexW + x) * 4;
                pixels[idx + 0] = v;
                pixels[idx + 1] = v;
                pixels[idx + 2] = v;
                pixels[idx + 3] = 255;
            }
        }

        // ── 2. Create VkImage with full mip chain ─────────────────────────────
        if (!VulkanMipmapGenerator::IsFormatSupported(device, kFmt))
        {
            LogVk("MipmapExample: linear blitting not supported — mip levels will be 1");
            g_MipLevels = 1;
        }
        else
        {
            g_MipLevels = VulkanMipmapGenerator::MipLevels(kTexW, kTexH);
            LogVk("MipmapExample: creating " + std::to_string(g_MipLevels) +
                  "-level mip chain for " + std::to_string(kTexW) +
                  "x" + std::to_string(kTexH) + " checkerboard");
        }

        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = kFmt;
        ici.extent        = { kTexW, kTexH, 1 };
        ici.mipLevels     = g_MipLevels;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT   // mip blit source
                          | VK_IMAGE_USAGE_TRANSFER_DST_BIT   // staging copy dest + blit dest
                          | VK_IMAGE_USAGE_SAMPLED_BIT;       // shader read
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        vmaCreateImage(device.GetAllocator(), &ici, &aci, &g_TexImage, &g_TexAlloc, nullptr);

        // ── 3. Upload base level via staging ──────────────────────────────────
        VkDeviceSize dataSize = kTexW * kTexH * 4;

        VulkanBuffer staging;
        staging.CreateStaging(device, dataSize);
        staging.Upload(pixels.data(), dataSize);

        // Transition level 0 to TRANSFER_DST so the staging copy can land
        VulkanOneTimeCommand otc;
        otc.Begin(device, command);
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = g_TexImage;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = g_MipLevels;  // all levels to DST
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            barrier.srcAccessMask                   = 0;
            barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(otc.Cmd(),
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Copy staging → image level 0
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = 1;
            region.imageExtent                     = { kTexW, kTexH, 1 };

            vkCmdCopyBufferToImage(otc.Cmd(), staging.GetBuffer(),
                g_TexImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }
        otc.End();
        staging.Shutdown();

        // ── 4. Generate mip chain ─────────────────────────────────────────────
        //  Image enters Generate() with level 0 in TRANSFER_DST_OPTIMAL
        //  (from the staging copy above) and levels 1..N in TRANSFER_DST_OPTIMAL
        //  (from the initial barrier spanning all levels).
        //  Generate() blits each level from the previous and transitions the
        //  entire chain to SHADER_READ_ONLY_OPTIMAL on exit.
        if (g_MipLevels > 1)
        {
            VulkanMipmapGenerator gen;
            gen.Generate(device, command, g_TexImage, kTexW, kTexH, g_MipLevels);
        }
        else
        {
            // Single-level fallback — just transition to SHADER_READ_ONLY
            VulkanOneTimeCommand otc2;
            otc2.Begin(device, command);
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                           = g_TexImage;
                barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;
                barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(otc2.Cmd(),
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            otc2.End();
        }

        // ── 5. Image view spanning all mip levels ─────────────────────────────
        VkImageViewCreateInfo vci{};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = g_TexImage;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = kFmt;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = g_MipLevels;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount     = 1;

        vkCreateImageView(device.GetDevice(), &vci, nullptr, &g_TexView);

        // ── 6. Sampler with LINEAR filter + full mip range ────────────────────
        //  minLod = 0, maxLod = g_MipLevels lets the GPU freely choose any level.
        //  Without this (e.g. maxLod = 0) the GPU always uses level 0 regardless
        //  of how small the texture appears on screen — mip generation would be
        //  wasted work.
        VkSamplerCreateInfo sci{};
        sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter    = VK_FILTER_NEAREST;           // no blurring when magnified
        sci.minFilter    = VK_FILTER_LINEAR;            // smooth when minified
        sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST; // snap to correct mip, no blending
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.minLod       = 0.0f;
        sci.maxLod       = static_cast<float>(g_MipLevels);
        sci.mipLodBias   = 0.0f;

        vkCreateSampler(device.GetDevice(), &sci, nullptr, &g_TexSampler);
    }

    void DestroyMipTexture()
    {
        VkDevice dev = device.GetDevice();
        if (g_TexSampler) { vkDestroySampler(dev, g_TexSampler, nullptr);   g_TexSampler = VK_NULL_HANDLE; }
        if (g_TexView)    { vkDestroyImageView(dev, g_TexView, nullptr);     g_TexView    = VK_NULL_HANDLE; }
        if (g_TexImage)   { vmaDestroyImage(device.GetAllocator(), g_TexImage, g_TexAlloc);
                            g_TexImage = VK_NULL_HANDLE; g_TexAlloc = VK_NULL_HANDLE; }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  DrawFrame
    // ─────────────────────────────────────────────────────────────────────────
    void DrawFrame()
    {
        if (g_Minimized || window_width == 0 || window_height == 0) return;

        if (g_Resized)
        {
            g_Resized = false;
            HandleResize();
            if (window_width == 0 || window_height == 0) return;
        }

        uint32_t frame = sync.GetCurrentFrameIndex();

        // Upload dummy UBO
        static const float kI[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        DummyUBO ubo{};
        std::memcpy(ubo.m, kI, sizeof(kI));
        uboBuffers[frame].Upload(&ubo, sizeof(ubo));

        // ── Acquire ───────────────────────────────────────────────────────────
        VkFence     fence      = sync.GetInFlightFence(frame);
        VkSemaphore imageReady = sync.GetImageAvailableSemaphore(frame);
        vkWaitForFences(device.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                              UINT64_MAX, imageReady, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { HandleResize(); return; }
        if (acq == VK_SUBOPTIMAL_KHR)          g_Resized = true;

        vkResetFences(device.GetDevice(), 1, &fence);

        // ── Record ────────────────────────────────────────────────────────────
        command.BeginRecording(frame);
        VkCommandBuffer cmd = command.GetCommandBuffer(frame);

        VkClearValue clear{};
        clear.color = { {0.08f, 0.08f, 0.10f, 1.0f} };

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = pipeline.GetRenderPass();
        rp.framebuffer       = framebuffers.Get(imageIndex);
        rp.renderArea.extent = swapchain.GetExtent();
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.width    = static_cast<float>(swapchain.GetExtent().width);
        vp.height   = static_cast<float>(swapchain.GetExtent().height);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.extent = swapchain.GetExtent();
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline.GetPipeline());

        std::array<VkDescriptorSet, 2> sets = { set0Sets[frame], set1Set };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            modelPipeline.GetPipelineLayout(), 0,
            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

        // Identity push constant
        float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        vkCmdPushConstants(cmd, modelPipeline.GetPipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT, 0, 64, identity);

        quad.RecordDraw(cmd);

        vkCmdEndRenderPass(cmd);
        command.EndRecording(frame);

        // ── Submit + present ──────────────────────────────────────────────────
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore          signalSem = sync.GetRenderFinishedSemaphore(frame);
        VkCommandBuffer      cmdBuf   = command.GetCommandBuffer(frame);

        VkSubmitInfo submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &imageReady;
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &cmdBuf;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &signalSem;
        vkQueueSubmit(device.GetGraphicsQueue(), 1, &submit, fence);

        VkSwapchainKHR sc = swapchain.GetSwapchain();

        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &signalSem;
        present.swapchainCount     = 1;
        present.pSwapchains        = &sc;
        present.pImageIndices      = &imageIndex;

        VkResult pres = vkQueuePresentKHR(device.GetPresentQueue(), &present);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR) HandleResize();
        else if (pres == VK_SUBOPTIMAL_KHR)   g_Resized = true;

        sync.AdvanceFrame();
    }

    void OnWindowRefresh(GLFWwindow*) { DrawFrame(); }

    // =========================================================================
    //  Init
    // =========================================================================
    void Init()
    {
        if (!glfwInit()) return;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

        window = glfwCreateWindow(window_width, window_height, title.c_str(), nullptr, nullptr);
        if (!window) { glfwTerminate(); return; }

        glfwSetFramebufferSizeCallback(window, OnFramebufferResize);
        glfwSetWindowRefreshCallback(window,   OnWindowRefresh);

        HWND hwnd = glfwGetWin32Window(window);

        context.Initialize(hwnd, title);
        device.Initialize(context.GetInstance(), context.GetSurface());
        swapchain.Initialize(device, context.GetSurface(), window_width, window_height);

        // Shaders: position (loc 0) + uv (loc 1) → texture sample
        shaders.VertexSpirv   = LoadSpv("./assets/mip.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/mip.frag.spv");

        vertexInput.Bindings = {{
            .binding   = 0,
            .stride    = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, uv)       },
        };

        pipeline.Initialize(device, swapchain.GetImageFormat(), shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        modelPipeline.Initialize(device, pipeline.GetRenderPass(), shaders, vertexInput);
        framebuffers.Initialize(device, swapchain, pipeline.GetRenderPass());

        // ── Descriptor pool: 1 UBO/frame + 1 sampler ─────────────────────────
        descAllocator.Initialize(device,
            MAX_FRAMES_IN_FLIGHT + 1,
            {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         MAX_FRAMES_IN_FLIGHT },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1                    },
            });

        // ── Per-frame UBO + set 0 ─────────────────────────────────────────────
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            uboBuffers[i].CreateUniform(device, sizeof(DummyUBO));
            set0Sets[i] = descAllocator.Allocate(modelPipeline.GetSet0Layout());

            VkDescriptorBufferInfo bi{};
            bi.buffer = uboBuffers[i].GetBuffer();
            bi.range  = sizeof(DummyUBO);

            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = set0Sets[i];
            w.dstBinding      = 0;
            w.descriptorCount = 1;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo     = &bi;
            vkUpdateDescriptorSets(device.GetDevice(), 1, &w, 0, nullptr);
        }

        // ── Build the mip-aware texture ───────────────────────────────────────
        CreateMipTexture();

        // ── Bind mip texture into set 1 ───────────────────────────────────────
        set1Set = descAllocator.Allocate(modelPipeline.GetSet1Layout());

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = g_TexSampler;
        imgInfo.imageView   = g_TexView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet imgWrite{};
        imgWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imgWrite.dstSet          = set1Set;
        imgWrite.dstBinding      = 0;
        imgWrite.descriptorCount = 1;
        imgWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imgWrite.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(device.GetDevice(), 1, &imgWrite, 0, nullptr);

        // ── Full-screen quad (two CCW triangles, UV [0,1]) ───────────────────
        //  Pipeline: VK_FRONT_FACE_COUNTER_CLOCKWISE + back-face cull.
        //  Viewed from the front (camera looking -Z), CCW means:
        //    tri 0: TL(0) → BL(3) → TR(1)
        //    tri 1: BL(3) → BR(2) → TR(1)
        //  UVs go to 4.0 so the checkerboard tiles 4x across — this makes
        //  the mip level selection visible: large tiles at full size, the
        //  pattern averaging toward grey as the window shrinks.
        const std::vector<Vertex> verts = {
            {{-1.f, -1.f, 0.f}, {0.f, 0.f}},   // 0 — top-left
            {{ 1.f, -1.f, 0.f}, {4.f, 0.f}},   // 1 — top-right
            {{ 1.f,  1.f, 0.f}, {4.f, 4.f}},   // 2 — bottom-right
            {{-1.f,  1.f, 0.f}, {0.f, 4.f}},   // 3 — bottom-left
        };
        const std::vector<uint32_t> indices = { 0,3,1, 3,2,1 };

        quad.Upload(device, command,
                    verts.data(), verts.size() * sizeof(Vertex),
                    indices.data(), static_cast<uint32_t>(indices.size()));
    }

    // =========================================================================
    //  Shutdown
    // =========================================================================
    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());

        DestroyMipTexture();
        quad.Shutdown();
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            uboBuffers[i].Shutdown();
        descAllocator.Shutdown();
        framebuffers.Shutdown();
        modelPipeline.Shutdown();

        sync.Shutdown();
        command.Shutdown();
        pipeline.Shutdown();
        swapchain.Shutdown();
        device.Shutdown();
        context.Shutdown();

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    // =========================================================================
    //  Run
    // =========================================================================
    void Run()
    {
        Init();
        while (!glfwWindowShouldClose(window))
        {
            if (g_Minimized) { glfwWaitEvents(); continue; }
            glfwPollEvents();
            DrawFrame();
        }
        Shutdown();
    }

} // namespace VCK::MipmapExample
