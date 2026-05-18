#include "App.h"
#include "VCK.h"
#include <array>
#include <cstdint>
#include <vector>
#include <fstream>

// =============================================================================
//  BindlessExample
//
//  4-slot bindless texture array.  Each quad face selects a texture by
//  pushing its array index via a push constant.  The fragment shader reads:
//    layout(set=0, binding=0) uniform sampler2D textures[];
//    layout(push_constant) uniform PC { uint texIdx; };
//    outColor = texture(textures[texIdx], inUV);
//
//  Requires cfg.device.enableBindless = true.
// =============================================================================

namespace VCK::BindlessExample {

    std::string title = "BindlessExample";
    Window      window;

    struct Vertex { float pos[2]; float uv[2]; };

    VulkanContext               context;
    VulkanDevice                device;
    VulkanSwapchain             swapchain;
    VulkanPipeline              pipeline;
    VulkanCommand               command;
    VulkanSync                  sync;
    VulkanFramebufferSet        framebuffers;
    VulkanMesh                  mesh;
    VulkanSampler               sampler;
    VulkanDescriptorAllocator   allocator;
    FrameScheduler              scheduler;

    static constexpr uint32_t kTextureCount = 4;
    std::array<VulkanTexture, kTextureCount> textures;

    VkDescriptorSetLayout        dsLayout = VK_NULL_HANDLE;
    VkPipelineLayout             pipeLayout = VK_NULL_HANDLE;

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

        VkCommandBuffer cmd = f.PrimaryCmd();

        VkClearValue clear{};
        clear.color = { {0.05f, 0.05f, 0.08f, 1.0f} };
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
        VkRect2D sc{}; sc.extent = swapchain.GetExtent();
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());

        VkDescriptorSet bs = allocator.GetBindlessSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout,
                                0, 1, &bs, 0, nullptr);

        for (uint32_t i = 0; i < kTextureCount; ++i)
        {
            vkCmdPushConstants(cmd, pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(uint32_t), &i);
            mesh.RecordDraw(cmd);
        }

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

        VulkanDevice::Config dcfg{};
        dcfg.enableBindless = true;
        device.Initialize(context, dcfg);

        swapchain.Initialize(device, context, window.GetWidth(), window.GetHeight());

        shaders.VertexSpirv   = LoadSpv("./assets/bindless.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/bindless.frag.spv");

        vertexInput.Bindings   = {{ .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, pos) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)  },
        };

        pipeline.Initialize(device, swapchain, shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        framebuffers.Initialize(device, swapchain, pipeline);

        FrameScheduler::Config fcfg{};
        scheduler.Initialize(device, command, sync, fcfg);

        sampler.Initialize(device);

        // Bindless: 4 COMBINED_IMAGE_SAMPLER slots.
        allocator.InitializeBindless(device, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kTextureCount);

        // Upload 4 solid-colour 1x1 textures and write them into the bindless array.
        const uint8_t colours[kTextureCount][4] = {
            {255,  64,  64, 255},  // red
            { 64, 255,  64, 255},  // green
            { 64,  64, 255, 255},  // blue
            {255, 255,  64, 255},  // yellow
        };
        for (uint32_t i = 0; i < kTextureCount; ++i)
        {
            textures[i].CreateFromPixels(device, command, colours[i], 1, 1);
            allocator.WriteBindless(i, textures[i].GetImageView(), sampler.GetSampler());
        }

        // Quad (two triangles).
        const Vertex verts[] = {
            {{-0.5f, -0.5f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f}, {0.0f, 1.0f}},
        };
        const uint32_t idx[] = { 0,1,2, 0,2,3 };
        mesh.Upload(device, command, verts, sizeof(verts), idx, 6);

        dsLayout   = allocator.GetBindlessLayout();
        pipeLayout = pipeline.GetLayout();

        VCKLog::Notice("BindlessExample", "Bindless texture array ready (4 slots).");
    }

    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());
        scheduler.Shutdown();
        for (auto& t : textures) t.Shutdown();
        sampler.Shutdown();
        allocator.Shutdown();
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

} // namespace VCK::BindlessExample
