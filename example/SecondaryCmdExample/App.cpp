#include "App.h"
#include "VCK.h"
#include <string>
#include <vector>
#include <fstream>

// =============================================================================
//  SecondaryCmdExample  (v0.3)
//
//  Draws an RGB triangle by recording the draw inside a secondary command
//  buffer and executing it from the scheduler's per-slot primary.
//
//  The primary command buffer still begins / ends the render pass; the
//  secondary only carries viewport + scissor + pipeline bind + draw.
//  vkCmdBeginRenderPass uses VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS
//  so the driver knows the pass will be populated via vkCmdExecuteCommands.
//
//  Secondaries are allocated once per slot at Init (from the same pool as
//  the primaries) and reset+re-recorded each frame.  The pool was created
//  with RESET_COMMAND_BUFFER_BIT by VulkanCommand, so individual resets
//  are legal.
// =============================================================================

namespace VCK::SecondaryCmdExample {

    std::string title         = "SecondaryCmdExample";
    VCK::Window window;
    int g_InitW = 1280;
    int g_InitH = 720;

    struct Vertex {
        float position[3];
        float color[4];
    };

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

    // One secondary per frame slot so we can re-record this slot's
    // secondary while the previous slot's secondary is still in flight
    // (rule 2 pattern: mirror MAX_FRAMES_IN_FLIGHT on anything per-slot).
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> secondaries{};

    std::vector<uint32_t> LoadSpv(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return {};
        size_t n = static_cast<size_t>(f.tellg());
        std::vector<uint32_t> buf(n / sizeof(uint32_t));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(buf.data()), n);
        return buf;
    }

    // Re-record `slot`'s secondary with the current viewport + draw.
    // The inheritance info ties this secondary to the pipeline's render
    // pass + subpass; the framebuffer field is optional but recommended
    // (some drivers use it to clip / optimise).
    void RecordSecondary(uint32_t slot, uint32_t imageIndex)
    {
        VkCommandBuffer cb = secondaries[slot];
        if (cb == VK_NULL_HANDLE) return;

        vkResetCommandBuffer(cb, 0);

        VkCommandBufferInheritanceInfo inh{};
        inh.sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inh.renderPass  = pipeline.GetRenderPass();
        inh.subpass     = 0;
        inh.framebuffer = framebuffers.Get(imageIndex);

        command.BeginSecondary(cb, inh);

        VkViewport vp{};
        vp.width    = static_cast<float>(swapchain.GetExtent().width);
        vp.height   = static_cast<float>(swapchain.GetExtent().height);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.extent = swapchain.GetExtent();
        vkCmdSetScissor(cb, 0, 1, &scissor);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());
        mesh.RecordDraw(cb);

        command.EndSecondary(cb);
    }

    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        // v0.3 scheduler-aware resize: drains scheduler fences/timeline
        // instead of vkDeviceWaitIdle.
        VCK::HandleLiveResize(window, swapchain, framebuffers, pipeline, scheduler);

        Frame& f = scheduler.BeginFrame();

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); return; }
        (void)acq;

        const uint32_t slot = scheduler.CurrentSlot();
        RecordSecondary(slot, imageIndex);

        VkClearValue clear{};
        clear.color = { {0.08f, 0.08f, 0.10f, 1.0f} };

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = pipeline.GetRenderPass();
        rp.framebuffer       = framebuffers.Get(imageIndex);
        rp.renderArea.extent = swapchain.GetExtent();
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        // SECONDARY_COMMAND_BUFFERS: the pass body comes from a subsequent
        // vkCmdExecuteCommands; the primary itself may not issue any
        // drawing commands between vkCmdBeginRenderPass and vkCmdEndRenderPass.
        vkCmdBeginRenderPass(f.PrimaryCmd(), &rp,
                             VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        VkCommandBuffer cb = secondaries[slot];
        VulkanCommand::ExecuteSecondaries(f.PrimaryCmd(), &cb, 1);
        vkCmdEndRenderPass(f.PrimaryCmd());

        GpuSubmissionBatcher::SubmitInfo si;
        si.waitSem   = f.ImageAvailable();
        si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(f.PrimaryCmd(), si);

        scheduler.EndFrame();

        VkSemaphore    signalSem = f.RenderFinished();
        VkSwapchainKHR sc        = swapchain.GetSwapchain();
        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &signalSem;
        present.swapchainCount     = 1;
        present.pSwapchains        = &sc;
        present.pImageIndices      = &imageIndex;
        VkResult pres = vkQueuePresentKHR(device.GetPresentQueue(), &present);
        (void)pres;
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

        shaders.VertexSpirv   = LoadSpv("./assets/secondary.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/secondary.frag.spv");

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

        FrameScheduler::Config cfg;
        cfg.policy         = FramePolicy::Pipelined;
        cfg.enableTimeline = true;  // shows the v0.3 frame-timeline path in the log
        scheduler.Initialize(device, command, sync, cfg);

        // Allocate per-slot secondaries once.  Pool has RESET_COMMAND_BUFFER_BIT
        // so vkResetCommandBuffer per frame is legal without resetting the
        // pool or destroying other slots' recordings.
        for (uint32_t i = 0; i < command.GetFramesInFlight(); ++i)
        {
            secondaries[i] = command.AllocateSecondary();
        }

        const std::vector<Vertex> vertices = {
            {{ 0.0f,  -0.6667f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
            {{-0.5f,   0.3333f, 0.f}, {0.f, 0.f, 1.f, 1.f}},
            {{ 0.5f,   0.3333f, 0.f}, {0.f, 1.f, 0.f, 1.f}},
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };
        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());

        for (VkCommandBuffer& cb : secondaries)
        {
            if (cb != VK_NULL_HANDLE) command.FreeSecondary(cb);
            cb = VK_NULL_HANDLE;
        }

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
            // Match the pattern every other example uses: block on events while
            // minimized so we don't burn CPU polling + early-returning from
            // DrawFrame when there is no surface to render into.
            if (window.IsMinimized()) { window.WaitEvents(); continue; }
            window.PollEvents();
            DrawFrame();
        }
        Shutdown();
    }
}
