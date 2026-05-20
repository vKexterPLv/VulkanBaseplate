#include "App.h"
#include "VCK.h"
#include <vector>
#include <fstream>

// =============================================================================
//  OffscreenTargetExample
//
//  Pass 1: render triangle to OffscreenTarget (R8G8B8A8_UNORM, window size).
//  Pass 2: FullscreenPass::Record samples the offscreen image and blits it
//          to the swapchain colour attachment.
// =============================================================================

namespace VCK::OffscreenTargetExample {

    std::string title = "OffscreenTargetExample";
    Window      window;

    struct Vertex { float pos[2]; float col[3]; };

    VulkanContext        context;
    VulkanDevice         device;
    VulkanSwapchain      swapchain;
    VulkanPipeline       offscreenPipeline;
    VulkanPipeline       swapchainPipeline;
    VulkanCommand        command;
    VulkanSync           sync;
    VulkanFramebufferSet swapchainFB;
    VulkanMesh           mesh;
    VulkanSampler        sampler;
    OffscreenTarget      offscreen;
    FullscreenPass       fullscreenPass;
    FrameScheduler       scheduler;

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::ShaderInfo      fsShaders;
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
        HandleLiveResize(window, device, swapchain, swapchainFB, swapchainPipeline);

        Frame& f = scheduler.BeginFrame();
        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(
            device.GetDevice(), swapchain.GetSwapchain(),
            UINT64_MAX, f.ImageAvailable(), VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); return; }

        VkCommandBuffer cmd = f.PrimaryCmd();

        // ── Pass 1: render triangle into offscreen target ─────────────────────
        VkClearValue offClear{};
        offClear.color = { {0.0f, 0.0f, 0.2f, 1.0f} };
        VkRenderPassBeginInfo rpOff{};
        rpOff.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpOff.renderPass        = offscreen.GetRenderPass();
        rpOff.framebuffer       = offscreen.GetFramebuffer();
        rpOff.renderArea.extent = offscreen.GetExtent();
        rpOff.clearValueCount   = 1;
        rpOff.pClearValues      = &offClear;
        vkCmdBeginRenderPass(cmd, &rpOff, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{};
        vp.width    = static_cast<float>(offscreen.GetExtent().width);
        vp.height   = static_cast<float>(offscreen.GetExtent().height);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{}; sc.extent = offscreen.GetExtent();
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, offscreenPipeline.GetPipeline());
        mesh.RecordDraw(cmd);
        vkCmdEndRenderPass(cmd);

        // ── Pass 2: fullscreen blit to swapchain ──────────────────────────────
        VkClearValue swClear{};
        swClear.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
        VkRenderPassBeginInfo rpSw{};
        rpSw.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpSw.renderPass        = swapchainPipeline.GetRenderPass();
        rpSw.framebuffer       = swapchainFB.Get(imageIndex);
        rpSw.renderArea.extent = swapchain.GetExtent();
        rpSw.clearValueCount   = 1;
        rpSw.pClearValues      = &swClear;
        vkCmdBeginRenderPass(cmd, &rpSw, VK_SUBPASS_CONTENTS_INLINE);

        vp.width  = static_cast<float>(swapchain.GetExtent().width);
        vp.height = static_cast<float>(swapchain.GetExtent().height);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        sc.extent = swapchain.GetExtent();
        vkCmdSetScissor(cmd, 0, 1, &sc);

        // FullscreenPass: bind descriptor + draw a fullscreen triangle that
        // samples the offscreen image via its push-descriptor set.
        fullscreenPass.Record(cmd, offscreen.GetImageView(), f.Slot());

        vkCmdEndRenderPass(cmd);

        GpuSubmissionBatcher::SubmitInfo si;
        si.waitSem   = f.ImageAvailable();
        si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(cmd, si);
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
        fsShaders.VertexSpirv   = LoadSpv("./assets/fullscreen.vert.spv");
        fsShaders.FragmentSpirv = LoadSpv("./assets/fullscreen.frag.spv");

        vertexInput.Bindings   = {{ .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, pos) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, col) },
        };

        offscreen.Initialize(device, VK_FORMAT_R8G8B8A8_UNORM,
                             { static_cast<uint32_t>(window.GetWidth()),
                               static_cast<uint32_t>(window.GetHeight()) });

        offscreenPipeline.Initialize(device, VK_FORMAT_R8G8B8A8_UNORM, shaders, vertexInput);
        swapchainPipeline.Initialize(device, swapchain, fsShaders, {});

        command.Initialize(device);
        sync.Initialize(device);
        swapchainFB.Initialize(device, swapchain, swapchainPipeline);
        sampler.Initialize(device);

        FrameScheduler::Config fcfg{};
        scheduler.Initialize(device, command, sync, fcfg);

        fullscreenPass.Initialize(device, swapchain, swapchainPipeline,
                                  fsShaders.VertexSpirv, fsShaders.FragmentSpirv,
                                  sampler.GetSampler(),
                                  scheduler.SlotCount());

        const Vertex verts[] = {
            {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        };
        const uint32_t idx[] = { 0, 1, 2 };
        mesh.Upload(device, command, verts, sizeof(verts), idx, 3);
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());
        scheduler.Shutdown();
        fullscreenPass.Shutdown();
        mesh.Shutdown();
        sampler.Shutdown();
        offscreen.Shutdown();
        swapchainFB.Shutdown();
        sync.Shutdown();
        command.Shutdown();
        offscreenPipeline.Shutdown();
        swapchainPipeline.Shutdown();
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

} // namespace VCK::OffscreenTargetExample
