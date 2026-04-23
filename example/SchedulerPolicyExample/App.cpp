#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>
#include <chrono>

// =============================================================================
//  SchedulerPolicyExample
//
//  Press 1 / 2 / 3 to switch FramePolicy at runtime.  On policy switch we
//  Shutdown the scheduler, wait for GPU idle, and Initialize it again with
//  the new config.  All other Vulkan objects (context/device/swapchain/
//  pipeline/command/sync/framebuffers/mesh) are untouched.
// =============================================================================

namespace VCK::SchedulerPolicyExample {

    std::string title         = "SchedulerPolicyExample";
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
    FramePolicy          currentPolicy = FramePolicy::Pipelined;

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

    uint64_t framesSinceSwitch = 0;
    uint64_t cpuUsSum          = 0;

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

    const char* PolicyName(FramePolicy p)
    {
        switch (p) {
            case FramePolicy::Lockstep:  return "Lockstep";
            case FramePolicy::Pipelined: return "Pipelined";
            case FramePolicy::AsyncMax:  return "AsyncMax";
        }
        return "?";
    }

    void ApplyPolicy(FramePolicy p)
    {
        if (p == currentPolicy) return;
        vkDeviceWaitIdle(device.GetDevice());
        scheduler.Shutdown();

        FrameScheduler::Config cfg;
        cfg.policy      = p;
        cfg.asyncMaxLag = 2;
        scheduler.Initialize(device, command, sync, cfg);

        currentPolicy = p;
        framesSinceSwitch = 0;
        cpuUsSum          = 0;

        std::string t = title + " - " + PolicyName(p);
        glfwSetWindowTitle(static_cast<GLFWwindow*>(window.NativeHandle()), t.c_str());
        LogVk(std::string("[SchedulerPolicy] switched to ") + PolicyName(p));
    }

    void OnKey(GLFWwindow*, int key, int, int action, int)
    {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_1) ApplyPolicy(FramePolicy::Lockstep);
        if (key == GLFW_KEY_2) ApplyPolicy(FramePolicy::Pipelined);
        if (key == GLFW_KEY_3) ApplyPolicy(FramePolicy::AsyncMax);
    }

    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        // Live resize: swapchain + framebuffers auto-rebuild when the OS
        // fires a framebuffer-size change (720p -> 4K / DPI / drag).
        VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);
        const auto frameT0 = std::chrono::steady_clock::now();

        Frame& f = scheduler.BeginFrame();

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); return; }
        (void)acq; // SUBOPTIMAL handled by HandleLiveResize next frame
        VkClearValue clear{};
        clear.color = { {0.05f, 0.08f, 0.12f, 1.0f} };
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
        const auto frameT1 = std::chrono::steady_clock::now();
        cpuUsSum += std::chrono::duration_cast<std::chrono::microseconds>(frameT1 - frameT0).count();
        ++framesSinceSwitch;
        if (framesSinceSwitch % 120 == 0)
        {
            LogVk("[SchedulerPolicy] policy=" + std::string(PolicyName(currentPolicy)) +
                  " avg_frame_us="   + std::to_string(cpuUsSum / 120) +
                  " gpu_retired="    + std::to_string(scheduler.Governor().GpuFrame()) +
                  " cpu_absolute="   + std::to_string(scheduler.AbsoluteFrame()));
            cpuUsSum = 0;
        }
    }

    void OnWindowRefresh() { DrawFrame(); }

    void Init()
    {
        VCK::WindowCreateInfo wci;
        wci.width     = g_InitW;
        wci.height    = g_InitH;
        wci.title     = title + " - Pipelined";
        wci.resizable = true;
        if (!window.Create(wci)) return;
        window.SetWindowRefreshCallback(OnWindowRefresh);
        // Extra GLFW callbacks retained through NativeHandle().  When VCK::Window
        // grows cross-platform input APIs these will move behind it as well.
        glfwSetKeyCallback(static_cast<GLFWwindow*>(window.NativeHandle()),             OnKey);
        context.Initialize(window, title);
        device.Initialize(context);
        swapchain.Initialize(device, context, window.GetWidth(), window.GetHeight());

        shaders.VertexSpirv   = LoadSpv("./assets/SchedulerPolicyExample.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/SchedulerPolicyExample.frag.spv");
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
            {{ 0.0f,  -0.6667f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
            {{-0.5f,   0.3333f, 0.f}, {0.f, 0.f, 1.f, 1.f}},
            {{ 0.5f,   0.3333f, 0.f}, {0.f, 1.f, 0.f, 1.f}},
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };
        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));

        LogVk("[SchedulerPolicyExample] press 1=Lockstep, 2=Pipelined, 3=AsyncMax.");
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

} // namespace VCK::SchedulerPolicyExample
