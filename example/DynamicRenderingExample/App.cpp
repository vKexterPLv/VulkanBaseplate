#include "App.h"
#include "VCK.h"
#include <cstdint>
#include <vector>
#include <fstream>

// =============================================================================
//  DynamicRenderingExample
//
//  A coloured triangle rendered via vkCmdBeginRendering / vkCmdEndRendering.
//  Swap-chain images are transitioned manually with vkCmdPipelineBarrier2.
//  Uses RGBTriangle's shader SPIR-V (triangle.vert.spv / triangle.frag.spv).
// =============================================================================

namespace VCK::DynamicRenderingExample {

    std::string title = "DynamicRenderingExample";
    Window      window;

    struct Vertex { float pos[2]; float col[3]; };

    VulkanContext   context;
    VulkanDevice    device;
    VulkanSwapchain swapchain;
    VulkanPipeline  pipeline;
    VulkanCommand   command;
    VulkanSync      sync;
    VulkanMesh      mesh;
    FrameScheduler  scheduler;

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

        Frame& f = scheduler.BeginFrame();

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(
            device.GetDevice(), swapchain.GetSwapchain(),
            UINT64_MAX, f.ImageAvailable(), VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { scheduler.EndFrame(); return; }

        VkCommandBuffer cmd = f.PrimaryCmd();

        // ── Transition: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL ─────────────────
        VkImageMemoryBarrier2 toAtt{};
        toAtt.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toAtt.srcStageMask     = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        toAtt.srcAccessMask    = 0;
        toAtt.dstStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        toAtt.dstAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        toAtt.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        toAtt.newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toAtt.image            = swapchain.GetImage(imageIndex);
        toAtt.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &toAtt;
        vkCmdPipelineBarrier2(cmd, &dep);

        // ── Dynamic rendering ─────────────────────────────────────────────────
        VkClearValue clear{};
        clear.color = { {0.06f, 0.07f, 0.09f, 1.0f} };

        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView   = swapchain.GetImageView(imageIndex);
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue  = clear;

        VkRenderingInfo ri{};
        ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent    = swapchain.GetExtent();
        ri.layerCount           = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &colorAtt;

        vkCmdBeginRendering(cmd, &ri);

        VkViewport vp{};
        vp.width    = static_cast<float>(swapchain.GetExtent().width);
        vp.height   = static_cast<float>(swapchain.GetExtent().height);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{}; sc.extent = swapchain.GetExtent();
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());
        mesh.RecordDraw(cmd);

        vkCmdEndRendering(cmd);

        // ── Transition: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR ──────────
        VkImageMemoryBarrier2 toPresent = toAtt;
        toPresent.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        dep.pImageMemoryBarriers = &toPresent;
        vkCmdPipelineBarrier2(cmd, &dep);

        // ── Submit + present ──────────────────────────────────────────────────
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

        VulkanDevice::Config dcfg{};
        dcfg.preferSync2 = true;
        device.Initialize(context, dcfg);

        VulkanSwapchain::Config scfg{};
        scfg.rendering.mode = RenderingMode::Dynamic;
        swapchain.Initialize(device, context, window.GetWidth(), window.GetHeight(), scfg);

        shaders.VertexSpirv   = LoadSpv("./assets/triangle.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/triangle.frag.spv");

        vertexInput.Bindings = {{
            .binding   = 0,
            .stride    = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,  .offset = offsetof(Vertex, pos) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, col) },
        };

        VulkanPipeline::Config pcfg{};
        pcfg.rendering.mode = RenderingMode::Dynamic;
        pcfg.dynamicColorFormat = swapchain.GetImageFormat();
        pipeline.Initialize(device, swapchain, shaders, vertexInput, pcfg);

        command.Initialize(device);
        sync.Initialize(device);

        FrameScheduler::Config fcfg{};
        fcfg.policy = FramePolicy::Pipelined;
        scheduler.Initialize(device, command, sync, fcfg);

        const Vertex verts[] = {
            {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        };
        const uint32_t idx[] = { 0, 1, 2 };
        mesh.Upload(device, command, verts, sizeof(verts), idx, 3);

        VCKLog::Notice("DynamicRenderingExample",
            "UsesDynamicRendering: " + std::string(device.HasDynamicRendering() ? "true" : "false"));
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());
        scheduler.Shutdown();
        mesh.Shutdown();
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

} // namespace VCK::DynamicRenderingExample
