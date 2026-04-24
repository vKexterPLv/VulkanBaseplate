#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>

// =============================================================================
//  TimelineExample
//
//  TimelineSemaphore + DependencyToken smoke test.
//
//  VulkanDevice currently does NOT enable the timelineSemaphore feature, so
//  TimelineSemaphore::Initialize returns false on most setups.  The example
//  handles both paths:
//
//    ok   → runs host Signal → host Wait round-trip every 60 frames.
//    fail → logs a single fallback line and keeps rendering normally.
// =============================================================================

namespace VCK::TimelineExample {

    std::string title         = "TimelineExample";
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

    TimelineSemaphore timeline;
    bool              timelineAvailable = false;
    uint64_t          timelineCounter   = 0;
    uint64_t          frameCounter      = 0;

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

    // Host-only timeline round-trip: bump the counter, observe it through
    // DependencyToken::WaitHost.  In a real pipeline the signal side would
    // come from vkQueueSubmit's VkTimelineSemaphoreSubmitInfo.
    void RunTimelineProbe()
    {
        if (!timelineAvailable) return;

        ++timelineCounter;
        if (!timeline.Signal(timelineCounter))
        {
            VCKLog::Warn("Timeline", "Signal failed - skipping wait.");
            return;
        }

        DependencyToken token{ &timeline, timelineCounter };
        const bool ok = token.WaitHost(/*timeoutNs*/ 1'000'000);   // 1 ms

        VCKLog::Info("Timeline", "counter=" + std::to_string(timelineCounter) +
              " wait=" + (ok ? "ok" : "timeout") +
              " last_signaled=" + std::to_string(timeline.LastSignaledValue()));
    }

    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        // Live resize: swapchain + framebuffers auto-rebuild when the OS
        // fires a framebuffer-size change (720p -> 4K / DPI / drag).
        VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);
        Frame& f = scheduler.BeginFrame();

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); return; }
        (void)acq; // SUBOPTIMAL handled by HandleLiveResize next frame
        VkClearValue clear{};
        clear.color = { {0.10f, 0.06f, 0.12f, 1.0f} };
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
        if ((++frameCounter % 60) == 0) RunTimelineProbe();
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

        shaders.VertexSpirv   = LoadSpv("./assets/TimelineExample.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/TimelineExample.frag.spv");
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
        cfg.policy = FramePolicy::Pipelined;
        scheduler.Initialize(device, command, sync, cfg);

        const std::vector<Vertex> vertices = {
            {{ 0.0f,  -0.6667f, 0.f}, {0.9f, 0.2f, 0.9f, 1.f}},
            {{-0.5f,   0.3333f, 0.f}, {0.2f, 0.9f, 0.9f, 1.f}},
            {{ 0.5f,   0.3333f, 0.f}, {0.9f, 0.9f, 0.2f, 1.f}},
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };
        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));

        // Try timeline - on most current builds this fails because the device
        // feature bit is not enabled.  Log and continue either way.
        timelineAvailable = timeline.Initialize(device, /*initial*/ 0);
        if (timelineAvailable)
        {
            VCKLog::Notice("TimelineExample", "TimelineSemaphore OK - will probe every 60 frames.");
        }
        else
        {
            VCKLog::Notice("TimelineExample", "TimelineSemaphore unavailable - "
                  "enable the timelineSemaphore device feature to exercise this path.");
        }
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());
        timeline.Shutdown();
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

} // namespace VCK::TimelineExample
