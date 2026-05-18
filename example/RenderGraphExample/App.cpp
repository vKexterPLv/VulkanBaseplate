#include "App.h"
#include "VCK.h"
#include <vector>
#include <fstream>

// =============================================================================
//  RenderGraphExample
//
//  Declares two passes and lets RenderGraph sort them and emit barriers.
// =============================================================================

namespace VCK::RenderGraphExample {

    std::string title = "RenderGraphExample";
    Window      window;

    struct Vertex { float pos[2]; float col[3]; };

    VulkanContext        context;
    VulkanDevice         device;
    VulkanSwapchain      swapchain;
    VulkanPipeline       pipeline;
    VulkanCommand        command;
    VulkanSync           sync;
    VulkanFramebufferSet framebuffers;
    VulkanMesh           mesh;
    FrameScheduler       scheduler;
    RenderGraph          graph;

    ResourceHandle offscreenRes = INVALID_RESOURCE;
    PassHandle     offscreenPass = INVALID_PASS;
    PassHandle     presentPass   = INVALID_PASS;

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

    std::vector<uint32_t> LoadSpv(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        size_t sz = static_cast<size_t>(f.tellg());
        std::vector<uint32_t> buf(sz / sizeof(uint32_t));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(buf.data()), sz);
        return buf;
    }

    void DrawFrame()
    {
        if (window.IsMinimized()) return;
        HandleLiveResize(window, device, swapchain, framebuffers, pipeline);

        Frame& f = scheduler.BeginFrame();
        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(
            device.GetDevice(), swapchain.GetSwapchain(),
            UINT64_MAX, f.ImageAvailable(), VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); return; }

        graph.Execute(f.PrimaryCmd(), f.Slot());

        GpuSubmissionBatcher::SubmitInfo si;
        si.waitSem   = f.ImageAvailable();
        si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(f.PrimaryCmd(), si);
        scheduler.EndFrame();

        VkSemaphore    sig = f.RenderFinished();
        VkSwapchainKHR sc2 = swapchain.GetSwapchain();
        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &sig;
        present.swapchainCount     = 1;
        present.pSwapchains        = &sc2;
        present.pImageIndices      = &imageIndex;
        (void)vkQueuePresentKHR(device.GetPresentQueue(), &present);
    }

    void OnWindowRefresh() { DrawFrame(); }

    void Init()
    {
        VCK::WindowCreateInfo wci;
        wci.width     = 1280;
        wci.height    = 720;
        wci.title     = title;
        wci.resizable = true;
        if (!window.Create(wci)) return;
        window.SetWindowRefreshCallback(OnWindowRefresh);

        context.Initialize(window, title);
        device.Initialize(context);
        swapchain.Initialize(device, context, window.GetWidth(), window.GetHeight());

        shaders.VertexSpirv   = LoadSpv("./assets/triangle.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/triangle.frag.spv");

        vertexInput.Bindings   = {{ .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, pos) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, col) },
        };

        pipeline.Initialize(device, swapchain, shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        framebuffers.Initialize(device, swapchain, pipeline);

        FrameScheduler::Config fcfg{};
        scheduler.Initialize(device, command, sync, fcfg);

        const Vertex verts[] = {
            {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        };
        const uint32_t idx[] = { 0, 1, 2 };
        mesh.Upload(device, command, verts, sizeof(verts), idx, 3);

        // ── Declare the two-pass render graph ─────────────────────────────────
        offscreenRes = graph.CreateTarget("colour",
            swapchain.GetImageFormat(),
            swapchain.GetExtent());

        offscreenPass = graph.AddPass("offscreen",
            [&](VkCommandBuffer cmd2, PassResources&)
            {
                VkClearValue clr{};
                clr.color = { {0.06f, 0.07f, 0.09f, 1.0f} };
                VkRenderPassBeginInfo rp{};
                rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp.renderPass        = pipeline.GetRenderPass();
                rp.framebuffer       = framebuffers.Get(0);
                rp.renderArea.extent = swapchain.GetExtent();
                rp.clearValueCount   = 1;
                rp.pClearValues      = &clr;
                vkCmdBeginRenderPass(cmd2, &rp, VK_SUBPASS_CONTENTS_INLINE);
                VkViewport vp{};
                vp.width    = static_cast<float>(swapchain.GetExtent().width);
                vp.height   = static_cast<float>(swapchain.GetExtent().height);
                vp.maxDepth = 1.0f;
                vkCmdSetViewport(cmd2, 0, 1, &vp);
                VkRect2D sc{}; sc.extent = swapchain.GetExtent();
                vkCmdSetScissor(cmd2, 0, 1, &sc);
                vkCmdBindPipeline(cmd2, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());
                mesh.RecordDraw(cmd2);
                vkCmdEndRenderPass(cmd2);
            });
        graph.Writes(offscreenPass, offscreenRes);

        presentPass = graph.AddPass("present",
            [&](VkCommandBuffer /*cmd2*/, PassResources& /*pr*/)
            {
                // In a real renderer: read pr.GetView(offscreenRes) and blit.
                VCKLog::Notice("RenderGraph", "present pass callback invoked.");
            });
        graph.Reads(presentPass, offscreenRes);

        if (!graph.Compile(device))
            VCKLog::Error("RenderGraphExample", "RenderGraph::Compile failed.");
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());
        graph.Shutdown();
        scheduler.Shutdown();
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

} // namespace VCK::RenderGraphExample
