#include "App.h"
#include "VulkanModule.h"
#include "VMM/VulkanMemoryManager.h"
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <cstring>
#include <cmath>

// =============================================================================
//  VMMExample
//
//  Three things draw simultaneously, each exercising a different VMM path:
//
//  [1] Static triangle  — Persistent GPU buffer + staging ring upload
//      Allocated once during Init(), never touched again.
//      vmm.AllocPersistent → vmm.StageToBuffer → vmm.FlushStaging
//
//  [2] Animated quad — TransientPool, rebuilt every frame
//      Vertex data is written directly into the mapped transient block.
//      vmm.AllocTransient → VmmBuffer::Upload (zero-copy into mapped memory)
//      The VkBuffer is shared — no vmaCreateBuffer every frame.
//
//  [3] Checkerboard texture — Persistent image + staging ring image upload
//      vmm.AllocPersistentImage → vmm.StageToImage → vmm.FlushStaging
//      Bound as a combined image/sampler; quad samples it so it's visible.
//
//  Frame boundary:
//      vmm.BeginFrame(frameIndex, absoluteFrame)  — resets transient slot
//      vmm.EndFrame(frameIndex)                   — submits batched staging
//
//  Every 120 frames:
//      vmm.LogStats()  →  VS Output window shows registry state + ring usage
// =============================================================================

namespace VulkanBaseplate::VMMExample {

    // ─────────────────────────────────────────────────────────────────────────
    //  Window
    // ─────────────────────────────────────────────────────────────────────────
    std::string title         = "VMMExample";
    GLFWwindow* window        = nullptr;
    int         window_width  = 1280;
    int         window_height = 720;

    bool     g_Resized      = false;
    bool     g_Minimized    = false;
    uint32_t g_AbsoluteFrame = 0;

    // ─────────────────────────────────────────────────────────────────────────
    //  Vertex layout — position + uv + color
    //  All three drawn objects share this layout.  The triangle ignores uv;
    //  the quad uses uv for texture sampling and ignores color.
    // ─────────────────────────────────────────────────────────────────────────
    struct Vertex {
        float position[3];   // location 0
        float uv[2];         // location 1
        float color[4];      // location 2
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Per-frame UBO — identity matrices; the shaders don't transform
    // ─────────────────────────────────────────────────────────────────────────
    struct alignas(16) FrameUBO { float m[16]; };

    // ─────────────────────────────────────────────────────────────────────────
    //  Core Vulkan objects
    // ─────────────────────────────────────────────────────────────────────────
    VulkanContext        context;
    VulkanDevice         device;
    VulkanSwapchain      swapchain;
    VulkanPipeline       pipeline;
    VulkanCommand        command;
    VulkanSync           sync;

    // ─────────────────────────────────────────────────────────────────────────
    //  Expansion objects
    // ─────────────────────────────────────────────────────────────────────────
    VulkanModelPipeline       modelPipeline;
    VulkanFramebufferSet      framebuffers;
    VulkanDescriptorAllocator descAllocator;

    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> set0Sets{};
    VkDescriptorSet                                   set1Set = VK_NULL_HANDLE;

    // Per-frame UBO buffers — allocated through VMM as FrameBuffered resources
    std::array<VmmBuffer, MAX_FRAMES_IN_FLIGHT> uboBuffers{};

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

    // ─────────────────────────────────────────────────────────────────────────
    //  VMM
    // ─────────────────────────────────────────────────────────────────────────
    VulkanMemoryManager vmm;

    // ── [1] Persistent triangle — uploaded once, never touched again ──────────
    VmmBuffer g_TriangleVBO;          // GPU-only vertex buffer
    VmmBuffer g_TriangleIBO;          // GPU-only index buffer
    uint32_t  g_TriangleIndexCount = 0;

    // ── [2] Animated quad — transient, rebuilt every frame ────────────────────
    //  These are VmmBuffer views into the transient block — NOT independent allocs.
    VmmBuffer g_QuadVBO;              // valid for current frame only
    VmmBuffer g_QuadIBO;              // valid for current frame only
    uint32_t  g_QuadIndexCount = 0;

    // ── [3] Persistent texture — VmmImage uploaded via staging ring ───────────
    VmmImage  g_Texture;
    VkSampler g_Sampler = VK_NULL_HANDLE;

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
    //  BuildTransientQuad
    //
    //  Called every frame.  Writes new vertex data directly into the transient
    //  block — no map/unmap, no allocation.  The quad corners oscillate on the
    //  Y axis so the animation is visible.
    // ─────────────────────────────────────────────────────────────────────────
    void BuildTransientQuad(uint32_t frameIndex)
    {
        float t = static_cast<float>(g_AbsoluteFrame) * 0.04f;
        float dy = std::sin(t) * 0.15f;

        // Full-screen quad with slight vertical bounce
        // UVs cover [0,1] so the texture tiles across it
        const Vertex verts[4] = {
            {{ -0.9f, -0.5f + dy, 0.f }, { 0.f, 0.f }, { 1,1,1,1 }},  // TL
            {{  0.9f, -0.5f + dy, 0.f }, { 1.f, 0.f }, { 1,1,1,1 }},  // TR
            {{  0.9f,  0.5f + dy, 0.f }, { 1.f, 1.f }, { 1,1,1,1 }},  // BR
            {{ -0.9f,  0.5f + dy, 0.f }, { 0.f, 1.f }, { 1,1,1,1 }},  // BL
        };
        const uint32_t indices[6] = { 0,1,2, 0,2,3 };
        g_QuadIndexCount = 6;

        // ── VMM Layer 3 — AllocTransient ──────────────────────────────────────
        //  Returns a view into the pre-allocated transient block for this slot.
        //  No VMA call, no allocation.  The cursor advances by sizeof(verts).
        g_QuadVBO = vmm.AllocTransient(frameIndex, "quad_vbo",
                                        sizeof(verts),
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        g_QuadIBO = vmm.AllocTransient(frameIndex, "quad_ibo",
                                        sizeof(indices),
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        // ── VmmBuffer::Upload — direct memcpy into mapped transient memory ─────
        //  mapped ptr is (block base + claimed offset) — computed at Claim() time.
        g_QuadVBO.Upload(verts,   sizeof(verts));
        g_QuadIBO.Upload(indices, sizeof(indices));
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

        // ── Wait for the previous cycle of this frame slot to finish ──────────
        //  This MUST happen before we touch any per-slot resources:
        //    • the transient block for this slot (reset by vmm.BeginFrame)
        //    • per-frame UBO backing memory (written below)
        //    • any overflow TransientFrame allocations from last cycle
        //  Otherwise we would modify memory the GPU is still reading.
        VkFence     fence      = sync.GetInFlightFence(frame);
        VkSemaphore imageReady = sync.GetImageAvailableSemaphore(frame);
        vkWaitForFences(device.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

        // ── VMM frame start ───────────────────────────────────────────────────
        //  Resets the transient block for this slot (cursor → 0).
        //  Frees any overflow TransientFrame registry entries from last cycle.
        vmm.BeginFrame(frame, g_AbsoluteFrame);

        // ── Rebuild animated quad into transient memory ───────────────────────
        BuildTransientQuad(frame);

        // ── Upload per-frame UBO ──────────────────────────────────────────────
        //  uboBuffers are CPU_TO_GPU with persistent map — direct memcpy.
        static const float kI[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        FrameUBO ubo{};
        std::memcpy(ubo.m, kI, sizeof(kI));
        uboBuffers[frame].Upload(&ubo, sizeof(ubo));

        // ── Acquire swapchain image ───────────────────────────────────────────
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
        clear.color = { {0.06f, 0.06f, 0.08f, 1.0f} };

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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          modelPipeline.GetPipeline());

        std::array<VkDescriptorSet, 2> sets = { set0Sets[frame], set1Set };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            modelPipeline.GetPipelineLayout(), 0,
            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

        // kI already declared above — reuse it for push constants
        vkCmdPushConstants(cmd, modelPipeline.GetPipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT, 0, 64, kI);

        // ── [2] Draw animated quad (transient VmmBuffers) ─────────────────────
        //  g_QuadVBO.buffer is a raw VkBuffer — same API as any other buffer.
        {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &g_QuadVBO.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, g_QuadIBO.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, g_QuadIndexCount, 1, 0, 0, 0);
        }

        // ── [1] Draw static triangle (persistent VmmBuffer) ───────────────────
        //  Same draw call pattern — VMM is transparent at the draw level.
        {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &g_TriangleVBO.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, g_TriangleIBO.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, g_TriangleIndexCount, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
        command.EndRecording(frame);

        // ── Submit ────────────────────────────────────────────────────────────
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

        // ── Present ───────────────────────────────────────────────────────────
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

        // ── VMM frame end ─────────────────────────────────────────────────────
        //  Submits any staging commands recorded this frame (none here since
        //  the static resources were uploaded and flushed during Init).
        //  Saves the ring tail position for this frame slot.
        vmm.EndFrame(frame);

        sync.AdvanceFrame();
        g_AbsoluteFrame++;

        // ── Periodic stats dump ───────────────────────────────────────────────
        if (g_AbsoluteFrame % 120 == 0)
            vmm.LogStats();
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

        // ── Core init (unchanged from any other example) ───────────────────────
        context.Initialize(hwnd, title);
        device.Initialize(context.GetInstance(), context.GetSurface());
        swapchain.Initialize(device, context.GetSurface(), window_width, window_height);

        shaders.VertexSpirv   = LoadSpv("./assets/vmm.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/vmm.frag.spv");

        // Vertex layout: position (loc 0) + uv (loc 1) + color (loc 2)
        vertexInput.Bindings = {{
            .binding   = 0,
            .stride    = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(Vertex, position) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,       .offset = offsetof(Vertex, uv)       },
            { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, color)    },
        };

        pipeline.Initialize(device, swapchain.GetImageFormat(), shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        modelPipeline.Initialize(device, pipeline.GetRenderPass(), shaders, vertexInput);
        framebuffers.Initialize(device, swapchain, pipeline.GetRenderPass());

        // ── VMM LAYER 3 — Initialize ──────────────────────────────────────────
        //  16 MB staging ring (smaller than default — fine for this example).
        //  4 MB transient blocks per frame slot.
        VulkanMemoryManager::Config vmmCfg;
        vmmCfg.stagingRingSize    = 16 * 1024 * 1024;
        vmmCfg.transientBlockSize =  4 * 1024 * 1024;
        vmm.Initialize(device, command, vmmCfg);

        // ── Descriptor pool ───────────────────────────────────────────────────
        descAllocator.Initialize(device,
            MAX_FRAMES_IN_FLIGHT + 1,
            {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         MAX_FRAMES_IN_FLIGHT },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1                    },
            });

        // ── VMM LAYER 3 — AllocPersistent (UBO buffers via VMM raw path) ──────
        //  These are per-frame UBOs.  We use VmmRawAlloc directly here to show
        //  Layer 1 explicitly — the VMM doesn't have a dedicated FrameBuffered
        //  allocator path yet, so raw alloc + manual lifetime is the right choice.
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            // VMM LAYER 1 — VmmRawAlloc::CreateUniform
            // CPU_TO_GPU + persistently mapped — no map/unmap per frame.
            uboBuffers[i] = VmmRawAlloc::CreateUniform(device, sizeof(FrameUBO));

            // VMM LAYER 2 — Register with Manual lifetime
            // Registry knows it exists; caller is responsible for freeing.
            ResourceInfo info{ Lifetime::Manual, 0, i, "frame_ubo" };
            vmm.Registry().Register(uboBuffers[i], info);

            // Wire into descriptor set
            set0Sets[i] = descAllocator.Allocate(modelPipeline.GetSet0Layout());

            VkDescriptorBufferInfo bi{};
            bi.buffer = uboBuffers[i].buffer;
            bi.range  = sizeof(FrameUBO);

            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = set0Sets[i];
            w.dstBinding      = 0;
            w.descriptorCount = 1;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo     = &bi;
            vkUpdateDescriptorSets(device.GetDevice(), 1, &w, 0, nullptr);
        }

        // ── VMM LAYER 3 — AllocPersistent + StageToBuffer ────────────────────
        //  Static triangle: vertices and indices live on the GPU forever.
        //  Data is pushed through the staging ring — one ring claim, one
        //  batched vkCmdCopyBuffer, submitted via FlushStaging().
        {
            const Vertex triVerts[3] = {
                {{  0.0f, -0.85f, 0.f }, { 0.5f, 0.f }, { 1.f, 0.2f, 0.2f, 1.f }},  // top    — red
                {{ -0.4f, -0.35f, 0.f }, { 0.f,  1.f }, { 0.2f, 0.2f, 1.f, 1.f }},  // left   — blue
                {{  0.4f, -0.35f, 0.f }, { 1.f,  1.f }, { 0.2f, 1.f, 0.2f, 1.f }},  // right  — green
            };
            const uint32_t triIdx[3] = { 0, 1, 2 };
            g_TriangleIndexCount = 3;

            // Allocate GPU-side buffers (GPU_ONLY, Persistent, registered)
            g_TriangleVBO = vmm.AllocPersistent("triangle_vbo",
                sizeof(triVerts),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

            g_TriangleIBO = vmm.AllocPersistent("triangle_ibo",
                sizeof(triIdx),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

            // Stage data — copies into the ring, records vkCmdCopyBuffer,
            // defers submission until FlushStaging / EndFrame.
            vmm.StageToBuffer(g_TriangleVBO, triVerts, sizeof(triVerts));
            vmm.StageToBuffer(g_TriangleIBO, triIdx,   sizeof(triIdx));
        }

        // ── VMM LAYER 3 — AllocPersistentImage + StageToImage ────────────────
        //  Procedural 128x128 checkerboard texture uploaded through the ring.
        {
            constexpr uint32_t kW = 128, kH = 128, kTile = 8;
            std::vector<uint8_t> pixels(kW * kH * 4);
            for (uint32_t y = 0; y < kH; y++) {
                for (uint32_t x = 0; x < kW; x++) {
                    bool     w   = ((x / kTile) + (y / kTile)) % 2 == 0;
                    uint8_t  v   = w ? 220 : 40;
                    uint32_t idx = (y * kW + x) * 4;
                    pixels[idx+0] = v; pixels[idx+1] = v;
                    pixels[idx+2] = v; pixels[idx+3] = 255;
                }
            }

            g_Texture = vmm.AllocPersistentImage(
                "checker_texture",
                kW, kH,
                VK_FORMAT_R8G8B8A8_SRGB,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);

            // StageToImage: transitions, copies from ring, transitions to
            // SHADER_READ_ONLY_OPTIMAL — all recorded into the staging cmd.
            vmm.StageToImage(g_Texture, pixels.data(),
                             static_cast<VkDeviceSize>(pixels.size()), kW, kH);

            // Linear sampler — no mipmapping in this example
            VkSamplerCreateInfo sci{};
            sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sci.magFilter    = VK_FILTER_LINEAR;
            sci.minFilter    = VK_FILTER_LINEAR;
            sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.maxLod       = 1.0f;
            vkCreateSampler(device.GetDevice(), &sci, nullptr, &g_Sampler);
        }

        // ── VMM LAYER 3 — FlushStaging ────────────────────────────────────────
        //  All StageToBuffer / StageToImage calls above are batched into one
        //  command buffer.  FlushStaging() ends it, submits it, and blocks until
        //  the GPU is done — safe to use at init time before the render loop.
        //  After this point the persistent buffers and texture are GPU-resident
        //  and the staging ring space is fully retired.
        vmm.FlushStaging();
        LogVk("VMMExample: FlushStaging complete — persistent resources on GPU");

        // Wire texture into set 1
        set1Set = descAllocator.Allocate(modelPipeline.GetSet1Layout());

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = g_Sampler;
        imgInfo.imageView   = g_Texture.view;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet imgWrite{};
        imgWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imgWrite.dstSet          = set1Set;
        imgWrite.dstBinding      = 0;
        imgWrite.descriptorCount = 1;
        imgWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imgWrite.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(device.GetDevice(), 1, &imgWrite, 0, nullptr);

        // Log initial registry state — shows 2 buffers (triangle vbo/ibo),
        // 1 image (checker_texture), and 2 Manual UBOs.
        vmm.LogStats();
    }

    // =========================================================================
    //  Shutdown  —  VMM before command/device
    // =========================================================================
    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());

        // ── VMM LAYER 1 — manually free the UBOs (registered as Manual) ───────
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            VmmRawAlloc::FreeBuffer(device, uboBuffers[i]);

        // ── VMM LAYER 3 — Shutdown frees all Persistent registry entries ───────
        //  This frees g_TriangleVBO, g_TriangleIBO, g_Texture (via registry),
        //  and the staging ring and transient blocks directly.
        vmm.Shutdown();

        // Sampler is not VMM-managed (it's not a buffer or image) — free manually
        if (g_Sampler) vkDestroySampler(device.GetDevice(), g_Sampler, nullptr);

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

} // namespace VulkanBaseplate::VMMExample
