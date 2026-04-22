#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>
#include <array>

// =============================================================================
//  SubmissionBatchingExample
//
//  Demonstrates GpuSubmissionBatcher: two independent command buffers
//  queued through the same Frame result in one vkQueueSubmit call.
//
//  An extra VkCommandPool ("pre" pool) is allocated alongside VulkanCommand
//  for the "pre" cmd buffer.  The "main" cmd buffer is the scheduler's own
//  primary cmd (VulkanCommand-managed).  EndFrame flushes the batcher
//  which submits them with one call.
// =============================================================================

namespace VCK::SubmissionBatchingExample {

    std::string title         = "SubmissionBatchingExample";
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

    // Extra per-slot cmd pool/buffer for the "pre" submission.
    VkCommandPool                                      preCmdPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT>  preCmds{};

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

    // Record a trivial "pre" command buffer.  In a real app this might do
    // image layout transitions, compute prep, or async uploads.  Here we
    // just record an empty buffer to keep the demo focused on batching.
    void RecordPreCmd(VkCommandBuffer cmd)
    {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
        // (no work - pretend it's a "prep" pass)
        VK_CHECK(vkEndCommandBuffer(cmd));
    }

    void DrawFrame()
    {
        if (g_Minimized || window_width == 0 || window_height == 0) return;
        if (g_Resized) { g_Resized = false; HandleResize(); if (window_width == 0) return; }

        Frame& f = scheduler.BeginFrame();

        // ── Record the "pre" cmd and queue it (no wait sema, no signal sema) ─
        VkCommandBuffer preCmd = preCmds[f.Slot()];
        RecordPreCmd(preCmd);
        GpuSubmissionBatcher::SubmitInfo preInfo;
        // pre has no sync requirements; it runs opportunistically.
        f.Submissions().QueueGraphics(preCmd, preInfo);

        // ── Acquire swapchain image for the main render pass ─────────────────
        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); HandleResize(); return; }
        if (acq == VK_SUBOPTIMAL_KHR) g_Resized = true;

        // ── Record the main render pass into the scheduler's primary cmd ─────
        VkClearValue clear{};
        clear.color = { {0.04f, 0.08f, 0.14f, 1.0f} };
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

        // ── Queue the main cmd with ImageAvailable/RenderFinished semas ──────
        GpuSubmissionBatcher::SubmitInfo mainInfo;
        mainInfo.waitSem   = f.ImageAvailable();
        mainInfo.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        mainInfo.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(f.PrimaryCmd(), mainInfo);

        // One vkQueueSubmit happens here (two VkCommandBuffers in one submit).
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

        if ((++frameCounter % 120) == 0)
        {
            LogVk("[SubmissionBatching] frame=" + std::to_string(frameCounter) +
                  " - each frame queued 2 cmd buffers via GpuSubmissionBatcher,"
                  " resulting in 1 vkQueueSubmit.");
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

        shaders.VertexSpirv   = LoadSpv("./assets/SubmissionBatchingExample.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/SubmissionBatchingExample.frag.spv");
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

        // Extra cmd pool/buffer for the "pre" submission.
        VkCommandPoolCreateInfo poolCi{};
        poolCi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCi.queueFamilyIndex = device.GetQueueFamilyIndices().GraphicsFamily.value();
        poolCi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(device.GetDevice(), &poolCi, nullptr, &preCmdPool));

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool        = preCmdPool;
        alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = static_cast<uint32_t>(preCmds.size());
        VK_CHECK(vkAllocateCommandBuffers(device.GetDevice(), &alloc, preCmds.data()));

        const std::vector<Vertex> vertices = {
            {{ 0.0f,  -0.6667f, 0.f}, {1.f, 0.5f, 0.2f, 1.f}},
            {{-0.5f,   0.3333f, 0.f}, {0.2f, 1.0f, 0.5f, 1.f}},
            {{ 0.5f,   0.3333f, 0.f}, {0.5f, 0.2f, 1.0f, 1.f}},
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };
        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));

        LogVk("[SubmissionBatchingExample] 2 cmd buffers → 1 vkQueueSubmit per frame.");
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());

        if (preCmdPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device.GetDevice(), preCmdPool, nullptr);
            preCmdPool = VK_NULL_HANDLE;
        }

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

} // namespace VCK::SubmissionBatchingExample
