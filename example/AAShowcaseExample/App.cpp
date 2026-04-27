#include "App.h"
#include "VCK.h"
#include <fstream>
#include <string>
#include <vector>

// =============================================================================
//  AAShowcaseExample
//
//  Shows the AA framework end-to-end: auto-detection, clamp behaviour, the
//  4-quadrant decision tree (forward|deferred x motion|no-motion), and the
//  per-swapchain readback API.
//
//  The draw loop is intentionally identical to RGBTriangle so the only
//  interesting diff between the two is the Init() block: we print every AA
//  decision the detector would make on this GPU before any Vulkan object is
//  created.  Once the swapchain boots with AATechnique::Auto the app echoes
//  what GOT picked, what MSAA sample count the renderpass settled on after
//  clamping to the GPU's framebufferColorSampleCounts mask, and whether
//  that technique is sample-based (wired by VCK) or post-process (the
//  renderer's responsibility - GetAATechnique() tells the renderer which
//  shader to run).
//
//  The triangle is the same RGB triangle from RGBTriangle; look closely at
//  its edges to see the AA: MSAA+A2C softens them; Off leaves them stepped.
// =============================================================================

namespace VCK::AAShowcaseExample {

    // -------------------------------------------------------------------------
    //  State
    // -------------------------------------------------------------------------
    std::string   title     = "AAShowcaseExample";
    VCK::Window   window;
    int           g_InitW   = 1280;
    int           g_InitH   = 720;

    struct Vertex {
        float position[3];
        float color[4];
    };

    VulkanContext                   context;
    VulkanDevice                    device;
    VulkanSwapchain                 swapchain;
    VulkanPipeline                  pipeline;
    VulkanCommand                   command;
    VulkanSync                      sync;
    VulkanFramebufferSet            framebuffers;
    VulkanMesh                      mesh;
    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

    // -------------------------------------------------------------------------
    //  Helpers
    // -------------------------------------------------------------------------
    static std::vector<uint32_t> LoadSpv(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> buf(size / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buf.data()), size);
        return buf;
    }

    static int MaxSamplesBits(VkSampleCountFlags f)
    {
        if (f & VK_SAMPLE_COUNT_64_BIT) return 64;
        if (f & VK_SAMPLE_COUNT_32_BIT) return 32;
        if (f & VK_SAMPLE_COUNT_16_BIT) return 16;
        if (f & VK_SAMPLE_COUNT_8_BIT)  return 8;
        if (f & VK_SAMPLE_COUNT_4_BIT)  return 4;
        if (f & VK_SAMPLE_COUNT_2_BIT)  return 2;
        return 1;
    }

    static int SampleBitsToInt(VkSampleCountFlagBits b)
    {
        switch (b) {
            case VK_SAMPLE_COUNT_1_BIT:  return 1;
            case VK_SAMPLE_COUNT_2_BIT:  return 2;
            case VK_SAMPLE_COUNT_4_BIT:  return 4;
            case VK_SAMPLE_COUNT_8_BIT:  return 8;
            case VK_SAMPLE_COUNT_16_BIT: return 16;
            case VK_SAMPLE_COUNT_32_BIT: return 32;
            case VK_SAMPLE_COUNT_64_BIT: return 64;
            default: return 1;
        }
    }

    // -------------------------------------------------------------------------
    //  [1] Print the decision tree before the swapchain is touched.
    //      Pure queries - no Vulkan objects are modified.
    // -------------------------------------------------------------------------
    static void PrintAADecisionMatrix(VkPhysicalDevice phys)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(phys, &props);

        VCKLog::Notice("AA-Showcase", "----- DetectRecommendedAA matrix -----");
        VCKLog::Notice("AA-Showcase", std::string("GPU      : ") + props.deviceName);

        const int maxC = MaxSamplesBits(props.limits.framebufferColorSampleCounts);
        const int maxD = MaxSamplesBits(props.limits.framebufferDepthSampleCounts);
        VCKLog::Notice("AA-Showcase",
            "Max MSAA : colour=" + std::to_string(maxC) + "x, depth=" + std::to_string(maxD) + "x");

        struct Row { bool fwd; bool mv; };
        const Row rows[] = {
            { true,  false },
            { true,  true  },
            { false, false },
            { false, true  },
        };
        for (const Row& r : rows) {
            AATechnique t = DetectRecommendedAA(phys, r.fwd, r.mv);
            std::string line;
            line += "forward=";       line += (r.fwd ? "yes" : "no ");
            line += "  motionVecs=";  line += (r.mv  ? "yes" : "no ");
            line += "  -> ";          line += ToString(t);
            line += IsSampleBasedAA(t) ? "  (sample-based)"
                                       : "  (post-process)";
            VCKLog::Notice("AA-Showcase", line);
        }
    }

    // -------------------------------------------------------------------------
    //  [2] Echo what the swapchain actually resolved to.
    // -------------------------------------------------------------------------
    static void PrintAAChoice()
    {
        const AATechnique picked = swapchain.GetAATechnique();
        const int samples        = SampleBitsToInt(swapchain.GetMSAASamples());

        VCKLog::Notice("AA-Showcase", "----- Swapchain resolved AA -----");
        VCKLog::Notice("AA-Showcase", std::string("picked           = ") + ToString(picked));
        VCKLog::Notice("AA-Showcase", "MSAA sample count= " + std::to_string(samples) + "x");
        VCKLog::Notice("AA-Showcase",
            std::string("category         = ") +
            (IsSampleBasedAA(picked) ? "sample-based (VCK implements)"
                                     : "post-process  (renderer implements the shader)"));
        if (IsPostProcessAA(picked)) {
            VCKLog::Notice("AA-Showcase",
                "NOTE: VCK only names this AA; call GetAATechnique() in your "
                "renderer to know which shader to run (FXAA / SMAA / TAA / TAAU).");
        }
    }

    // -------------------------------------------------------------------------
    //  Draw loop  -  same as RGBTriangle (minus comments, same structure).
    // -------------------------------------------------------------------------
    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);
        uint32_t frame = sync.GetCurrentFrameIndex();

        VkFence     fence      = sync.GetInFlightFence(frame);
        VkSemaphore imageReady = sync.GetImageAvailableSemaphore(frame);
        vkWaitForFences(device.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, imageReady, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { return; }
        (void)acq;
        vkResetFences(device.GetDevice(), 1, &fence);

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

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore          signalSem = sync.GetRenderFinishedSemaphore(frame);
        VkCommandBuffer      cmdBuf    = command.GetCommandBuffer(frame);

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
        (void)pres;
        sync.AdvanceFrame();
    }

    static void OnWindowRefresh() { DrawFrame(); }

    // -------------------------------------------------------------------------
    //  Init / Shutdown
    // -------------------------------------------------------------------------
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
        device .Initialize(context);

        // Print the matrix BEFORE the swapchain commits to a technique.
        PrintAADecisionMatrix(device.GetPhysicalDevice());

        // Swap with AATechnique::Auto (the default on Config::aa).
        swapchain.Initialize(device, context, window.GetWidth(), window.GetHeight());
        PrintAAChoice();

        shaders.VertexSpirv   = LoadSpv("./assets/aa.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/aa.frag.spv");

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

        // Same NDC triangle as RGBTriangle so the visual baseline is
        // identical - only the AA pick varies between runs.
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
        mesh        .Shutdown();
        framebuffers.Shutdown();
        sync        .Shutdown();
        command     .Shutdown();
        pipeline    .Shutdown();
        swapchain   .Shutdown();
        device      .Shutdown();
        context     .Shutdown();
        window      .Destroy();
    }

    void Run()
    {
        Init();
        while (!window.ShouldClose()) {
            window.PollEvents();
            DrawFrame();
        }
        Shutdown();
    }

} // namespace VCK::AAShowcaseExample
