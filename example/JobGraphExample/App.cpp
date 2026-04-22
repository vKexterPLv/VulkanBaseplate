#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>
#include <atomic>
#include <chrono>
#include <thread>

// =============================================================================
//  JobGraphExample
//
//  Frame::Jobs() exposes the JobGraph used for CPU-side work this frame.
//  This example schedules four jobs with dependencies and runs them every
//  frame via scheduler.DispatchJobs().
//
//      [physics] ──┐
//                  ├──> [build-cmds]
//      [animate] ──┘
//      [audio]                              (parallel, no deps)
//
//  Every 60 frames the average duration of each job is logged.
// =============================================================================

namespace VCK::JobGraphExample {

    std::string title         = "JobGraphExample";
    GLFWwindow* window        = nullptr;
    int         window_width  = 1280;
    int         window_height = 720;

    bool g_Resized   = false;
    bool g_Minimized = false;

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

    // Per-job accumulators (reset every 60 frames).
    struct JobStats {
        std::atomic<uint64_t> totalUs{0};
        std::atomic<uint64_t> samples{0};
        void Add(uint64_t us) { totalUs += us; samples += 1; }
        uint64_t Average()    { auto s = samples.load(); return s ? totalUs.load() / s : 0; }
        void Reset()          { totalUs = 0; samples = 0; }
    };
    JobStats physicsStats, animateStats, audioStats, buildStats;

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

    void HandleResize()
    {
        if (window_width == 0 || window_height == 0) return;
        vkDeviceWaitIdle(device.GetDevice());
        swapchain.Recreate(window_width, window_height);
        framebuffers.Recreate(pipeline);
    }

    void OnFramebufferResize(GLFWwindow*, int w, int h)
    {
        window_width = w; window_height = h;
        if (w == 0 || h == 0) { g_Minimized = true; return; }
        g_Minimized = false;
        g_Resized   = true;
    }

    // Timed job — sleeps a deterministic amount of work and accumulates
    // measured duration into `stats`.
    static void TimedJob(JobStats& stats, std::chrono::microseconds work)
    {
        const auto t0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(work);
        const auto t1 = std::chrono::steady_clock::now();
        stats.Add(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()));
    }

    void DrawFrame()
    {
        if (g_Minimized || window_width == 0 || window_height == 0) return;
        if (g_Resized) { g_Resized = false; HandleResize(); if (window_width == 0) return; }

        Frame& f = scheduler.BeginFrame();

        // ── Register CPU jobs for this frame ─────────────────────────────────
        auto jPhysics = f.Jobs().Add("physics",  [] { TimedJob(physicsStats,  std::chrono::microseconds(500)); });
        auto jAnimate = f.Jobs().Add("animate",  [] { TimedJob(animateStats,  std::chrono::microseconds(400)); });
        /* audio  */   f.Jobs().Add("audio",    [] { TimedJob(audioStats,    std::chrono::microseconds(200)); });
        /* build  */   f.Jobs().Add("build-cmds",
                                     [] { TimedJob(buildStats,    std::chrono::microseconds(300)); },
                                     { jPhysics, jAnimate });

        scheduler.DispatchJobs();   // runs all four, blocks until done

        // ── Acquire + render ─────────────────────────────────────────────────
        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); HandleResize(); return; }
        if (acq == VK_SUBOPTIMAL_KHR) g_Resized = true;

        VkClearValue clear{};
        clear.color = { {0.08f, 0.10f, 0.12f, 1.0f} };
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
        if (pres == VK_ERROR_OUT_OF_DATE_KHR) HandleResize();
        else if (pres == VK_SUBOPTIMAL_KHR)   g_Resized = true;

        // ── Periodic stats dump ──────────────────────────────────────────────
        if ((++frameCounter % 60) == 0)
        {
            LogVk("[JobGraph] avg us/frame: physics=" + std::to_string(physicsStats.Average()) +
                  " animate=" + std::to_string(animateStats.Average()) +
                  " audio="   + std::to_string(audioStats.Average()) +
                  " build="   + std::to_string(buildStats.Average()));
            physicsStats.Reset(); animateStats.Reset();
            audioStats.Reset();   buildStats.Reset();
        }
    }

    void OnWindowRefresh(GLFWwindow*) { DrawFrame(); }

    void Init()
    {
        if (!glfwInit()) return;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);
        window = glfwCreateWindow(window_width, window_height, title.c_str(), nullptr, nullptr);
        if (!window) { glfwTerminate(); return; }
        glfwSetFramebufferSizeCallback(window, OnFramebufferResize);
        glfwSetWindowRefreshCallback(window,   OnWindowRefresh);

        context.Initialize(glfwGetWin32Window(window), title);
        device.Initialize(context);
        swapchain.Initialize(device, context, window_width, window_height);

        shaders.VertexSpirv   = LoadSpv("./assets/JobGraphExample.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/JobGraphExample.frag.spv");
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
        cfg.policy     = FramePolicy::Pipelined;
        cfg.jobWorkers = 0;                       // 0 → hardware_concurrency
        scheduler.Initialize(device, command, sync, cfg);

        const std::vector<Vertex> vertices = {
            {{ 0.0f,  -0.6667f, 0.f}, {1.f, 0.2f, 0.6f, 1.f}},
            {{-0.5f,   0.3333f, 0.f}, {0.2f, 0.6f, 1.0f, 1.f}},
            {{ 0.5f,   0.3333f, 0.f}, {0.6f, 1.0f, 0.2f, 1.f}},
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };
        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));

        LogVk("[JobGraphExample] scheduler + 4-job CPU graph.  Logs averages every 60 frames.");
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());
        scheduler.Shutdown();
        mesh.Shutdown();
        framebuffers.Shutdown();
        sync.Shutdown(); command.Shutdown(); pipeline.Shutdown();
        swapchain.Shutdown(); device.Shutdown(); context.Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
    }

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

} // namespace VCK::JobGraphExample
