#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>

// =============================================================================
//  RGBTriangle
//
//  The smallest possible VCK program that draws something and
//  handles live window resize correctly.
//
//  Objects used:
//    Core     - VulkanContext, VulkanDevice, VulkanSwapchain,
//               VulkanPipeline, VulkanCommand, VulkanSync
//    Expansion - VulkanFramebufferSet, VulkanMesh
//
//  VulkanPipeline is used directly - its built-in empty layout is exactly
//  right here since we have no UBOs, no descriptors, and no push constants.
//  The vertex shader reads position + color straight from the vertex buffer.
//
//  Resize path:
//    VCK::Window auto-tracks framebuffer size via its internal GLFW callback.
//    OnWindowRefresh calls DrawFrame() during the OS drag loop so the window
//    keeps painting while the user is actively resizing.
//    VCK::HandleLiveResize() runs once per frame; when the Window's resize
//    latch is set it does vkDeviceWaitIdle + swapchain.Recreate +
//    framebuffers.Recreate in place (no rebuild).
// =============================================================================

namespace VCK::RGBTriangle {

    // ─────────────────────────────────────────────────────────────────────────
    //  Window state
    // ─────────────────────────────────────────────────────────────────────────
    std::string title         = "RGBTriangle";
    VCK::Window window;
    int g_InitW = 1280;
    int g_InitH = 720;
    // ─────────────────────────────────────────────────────────────────────────
    //  Vertex - position + color only.  No normals, no UVs needed.
    // ─────────────────────────────────────────────────────────────────────────
    struct Vertex {
        float position[3];   // location 0
        float color[4];      // location 1
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Vulkan objects
    // ─────────────────────────────────────────────────────────────────────────
    VulkanContext        context;
    VulkanDevice         device;
    VulkanSwapchain      swapchain;
    VulkanPipeline       pipeline;     // owns VkRenderPass, empty descriptor layout
    VulkanCommand        command;
    VulkanSync           sync;

    VulkanFramebufferSet framebuffers;
    VulkanMesh           mesh;

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

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

    // ─────────────────────────────────────────────────────────────────────────
    //  DrawFrame
    // ─────────────────────────────────────────────────────────────────────────
    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        // Live resize: swapchain + framebuffers auto-rebuild when the
        // OS fires a framebuffer-size change (720p -> 4K / DPI / drag).
        VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);
        uint32_t frame = sync.GetCurrentFrameIndex();

        // ── Fence wait + acquire ──────────────────────────────────────────────
        VkFence     fence      = sync.GetInFlightFence(frame);
        VkSemaphore imageReady = sync.GetImageAvailableSemaphore(frame);
        vkWaitForFences(device.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                              UINT64_MAX, imageReady, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { return; }
        (void)acq; // SUBOPTIMAL handled by HandleLiveResize next frame
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());

        mesh.RecordDraw(cmd);

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
        (void)pres; // OUT_OF_DATE handled by HandleLiveResize next frame
        sync.AdvanceFrame();
    }

    void OnWindowRefresh() { DrawFrame(); }

    // =========================================================================
    //  Init
    // =========================================================================
    void Init()
    {
        VCK::WindowCreateInfo wci;
        wci.width     = g_InitW;
        wci.height    = g_InitH;
        wci.title     = title;
        wci.resizable = true;
        if (!window.Create(wci)) return;
        window.SetWindowRefreshCallback(OnWindowRefresh);
        context.Initialize(window, title);
        device.Initialize(context);
        swapchain.Initialize(device, context, window.GetWidth(), window.GetHeight());

        // Shaders: position (location 0) + color (location 1) → output color.
        // No descriptor sets, no push constants.
        shaders.VertexSpirv   = LoadSpv("./assets/triangle.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/triangle.frag.spv");

        vertexInput.Bindings = {{
            .binding   = 0,
            .stride    = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(Vertex, position) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, color)    },
        };

        pipeline.Initialize(device, swapchain, shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        framebuffers.Initialize(device, swapchain, pipeline);

        // ── RGB triangle, CCW winding, NDC-space ──────────────────────────────
        const std::vector<Vertex> vertices = {
            {{ 0.0f,  -0.6667f, 0.f}, {1.f, 0.f, 0.f, 1.f}},  // top          - red
            {{-0.5f,   0.3333f, 0.f}, {0.f, 0.f, 1.f, 1.f}},  // bottom-left  - blue
            {{ 0.5f,   0.3333f, 0.f}, {0.f, 1.f, 0.f, 1.f}},  // bottom-right - green
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };

        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));
    }

    // =========================================================================
    //  Shutdown  -  expansion objects first, then base objects
    // =========================================================================
    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());

        mesh.Shutdown();
        framebuffers.Shutdown();

        sync.Shutdown();
        command.Shutdown();
        pipeline.Shutdown();
        swapchain.Shutdown();
        device.Shutdown();
        context.Shutdown();

        window.Destroy();
    }

    // =========================================================================
    //  Run
    // =========================================================================
    void Run()
    {
        Init();
        while (!window.ShouldClose())
        {
            if (window.IsMinimized()) { window.WaitEvents(); continue; }
            window.PollEvents();
            DrawFrame();
        }
        Shutdown();
    }

} // namespace VCK::RGBTriangle
