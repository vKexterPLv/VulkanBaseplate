#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>

// =============================================================================
//  DebugTimelineExample
//
//  Exercises the DebugTimeline recorder that ships with FrameScheduler.
//
//    - cfg.enableTimeline = true            → scheduler records "frame"
//      and "jobs" spans for every frame automatically.
//    - this example adds "simulate" and "record" spans around the user
//      code to show how application code participates.
//    - NoteStall() demonstrates annotating an explicit bubble.
//    - Every 120 frames Dump() flushes the accumulated spans to LogVk.
// =============================================================================

namespace VCK::DebugTimelineExample {

    std::string title         = "DebugTimelineExample";
    VCK::Window window;
    int g_InitW = 1280;
    int g_InitH = 720;
    struct Vertex { float position[3]; float color[4]; };

    VulkanContext        context;
    VulkanDevice         device;
    VulkanSwapchain      swapchain;
    VulkanPipeline       pipeline;
    VulkanCommand        command;
    VulkanSync           sync;
    VulkanFramebufferSet framebuffers;
    VulkanMesh           mesh;
    FrameScheduler       scheduler;

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

    uint64_t frameCounter = 0;

    std::vector<uint32_t> LoadSpv(const std::string& p)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return {};
        size_t n = static_cast<size_t>(f.tellg());
        std::vector<uint32_t> buf(n / sizeof(uint32_t));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(buf.data()), n);
        return buf;
    }

    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        // Live resize: swapchain + framebuffers auto-rebuild when the OS
        // fires a framebuffer-size change (720p -> 4K / DPI / drag).
        VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);
        Frame& f = scheduler.BeginFrame();
        auto&  dt = scheduler.Timeline();

        // ── User-added CPU span around a simulated "simulate" stage ──────────
        if (dt.Enabled()) dt.BeginCpuSpan("simulate", f.Absolute());
        std::this_thread::sleep_for(std::chrono::microseconds(250));
        if (dt.Enabled()) dt.EndCpuSpan("simulate", f.Absolute());

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); return; }
        (void)acq; // SUBOPTIMAL handled by HandleLiveResize next frame
        if (dt.Enabled()) dt.BeginCpuSpan("record", f.Absolute());

        VkClearValue clear{};
        clear.color = { {0.12f, 0.06f, 0.10f, 1.0f} };
        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = pipeline.GetRenderPass();
        rp.framebuffer       = framebuffers.Get(imageIndex);
        rp.renderArea.extent = swapchain.GetExtent();
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;
        vkCmdBeginRenderPass(f.PrimaryCmd(), &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.width    = static_cast<float>(swapchain.GetExtent().width);
        vp.height   = static_cast<float>(swapchain.GetExtent().height);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(f.PrimaryCmd(), 0, 1, &vp);
        VkRect2D sc{}; sc.extent = swapchain.GetExtent();
        vkCmdSetScissor(f.PrimaryCmd(), 0, 1, &sc);

        vkCmdBindPipeline(f.PrimaryCmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());
        mesh.RecordDraw(f.PrimaryCmd());
        vkCmdEndRenderPass(f.PrimaryCmd());

        if (dt.Enabled()) dt.EndCpuSpan("record", f.Absolute());

        // Fake a stall note every 30th frame to exercise NoteStall().
        if (dt.Enabled() && (f.Absolute() % 30 == 0))
        {
            dt.NoteStall("demo-stall", f.Absolute(), /*us*/ 150);
        }

        GpuSubmissionBatcher::SubmitInfo si;
        si.waitSem   = f.ImageAvailable();
        si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(f.PrimaryCmd(), si);
        scheduler.EndFrame();

        VkSemaphore    signalSem = f.RenderFinished();
        VkSwapchainKHR swap      = swapchain.GetSwapchain();
        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &signalSem;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &swap;
        pi.pImageIndices      = &imageIndex;
        VkResult pres = vkQueuePresentKHR(device.GetPresentQueue(), &pi);
        (void)pres; // OUT_OF_DATE handled by HandleLiveResize next frame
        if ((++frameCounter % 120) == 0)
        {
            LogVk("[DebugTimeline] ── dumping 120 frames of spans ──");
            scheduler.Timeline().Dump();
        }
    }

    void OnWindowRefresh() { DrawFrame(); }

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

        shaders.VertexSpirv   = LoadSpv("./assets/DebugTimelineExample.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/DebugTimelineExample.frag.spv");
        vertexInput.Bindings = {{ .binding=0, .stride=sizeof(Vertex), .inputRate=VK_VERTEX_INPUT_RATE_VERTEX }};
        vertexInput.Attributes = {
            { .location=0, .binding=0, .format=VK_FORMAT_R32G32B32_SFLOAT,    .offset=offsetof(Vertex,position) },
            { .location=1, .binding=0, .format=VK_FORMAT_R32G32B32A32_SFLOAT, .offset=offsetof(Vertex,color)    },
        };
        pipeline.Initialize(device, swapchain, shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        framebuffers.Initialize(device, swapchain, pipeline);

        FrameScheduler::Config cfg;
        cfg.policy         = FramePolicy::Pipelined;
        cfg.enableTimeline = true;                  // record spans
        scheduler.Initialize(device, command, sync, cfg);

        const std::vector<Vertex> vertices = {
            {{ 0.0f,  -0.6667f, 0.f}, {1.f, 0.3f, 0.3f, 1.f}},
            {{-0.5f,   0.3333f, 0.f}, {0.3f, 1.f, 0.3f, 1.f}},
            {{ 0.5f,   0.3333f, 0.f}, {0.3f, 0.3f, 1.f, 1.f}},
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };
        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));

        LogVk("[DebugTimelineExample] DebugTimeline enabled; Dump() every 120 frames.");
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());
        scheduler.Shutdown();
        mesh.Shutdown();
        framebuffers.Shutdown();
        sync.Shutdown(); command.Shutdown(); pipeline.Shutdown();
        swapchain.Shutdown(); device.Shutdown(); context.Shutdown();
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

} // namespace VCK::DebugTimelineExample
