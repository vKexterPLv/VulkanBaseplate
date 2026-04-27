#include "App.h"
#include "VCK.h"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// =============================================================================
//  EasyCubeExample  -  implementation
//
//  Rendering path (same shape as HelloExample):
//    Init:
//      window -> context -> device -> swapchain -> shaders ->
//      VertexLayout (Add("position", Vec3).Add("normal", Vec3)) ->
//      PushConstants (Declare("mvp", Mat4)) + Range() ->
//      VulkanPipeline::Initialize (vertex input + pc range from above) ->
//      command -> sync -> framebuffers -> scheduler ->
//      VCK::Primitives::Cube() -> interleaved upload to VulkanMesh.
//
//    DrawFrame:
//      Compute angle -> pc.Set("mvp", proj * view * rotate * scale).
//      BeginFrame -> acquire -> BeginRenderPass ->
//      BindPipeline -> pc.Apply(cb, layout, VK_SHADER_STAGE_VERTEX_BIT) ->
//      mesh.RecordDraw(cb) -> EndRenderPass -> QueueGraphics -> EndFrame ->
//      vkQueuePresentKHR.
//
//  The new API lines (VertexLayout, PushConstants, Primitives::Cube,
//  VCKMath) add up to under 20 lines total in Init + DrawFrame.  The rest
//  is boilerplate that every scheduler-driven example has regardless.
// =============================================================================

namespace VCK::EasyCubeExample {

    // ─────────────────────────────────────────────────────────────────────────
    //  Window state
    // ─────────────────────────────────────────────────────────────────────────
    std::string title = "EasyCubeExample - Primitives::Cube + VertexLayout + PushConstants";
    VCK::Window window;
    int g_InitW = 1280;
    int g_InitH = 720;

    // ─────────────────────────────────────────────────────────────────────────
    //  Vulkan objects
    // ─────────────────────────────────────────────────────────────────────────
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

    // The v0.3 public classes we're actually demonstrating.
    VertexLayout   vlayout;
    PushConstants  pc;

    // Cache of the Cube's interleaved vertex buffer (position + normal).
    // VulkanMesh::Upload takes one contiguous chunk, so we pack the two
    // Vec3 streams Primitives::Cube emits into {pos, normal} pairs.
    struct V { Vec3 position; Vec3 normal; };
    static_assert(sizeof(V) == 24, "Vec3 must be tightly packed");

    std::vector<V>        g_Verts;
    std::vector<uint32_t> g_Indices;

    auto g_StartTime = std::chrono::steady_clock::now();

    // ─────────────────────────────────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<uint32_t> LoadSpv(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> buf(size / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buf.data()), size);
        return buf;
    }

    Mat4 ComputeMVP()
    {
        const float dt = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - g_StartTime).count();

        const uint32_t w = swapchain.GetExtent().width;
        const uint32_t h = swapchain.GetExtent().height;
        const float aspect = (h == 0) ? 1.0f
                           : static_cast<float>(w) / static_cast<float>(h);

        // V C K M a t h  A P I  ─────────────────────────────────────────────
        Mat4 proj  = Perspective(Radians(60.0f), aspect, 0.1f, 100.0f);
        Mat4 view  = LookAt({ 2.2f, 1.6f, 2.2f },
                            { 0.0f, 0.0f, 0.0f },
                            { 0.0f, 1.0f, 0.0f });
        Mat4 model = Rotate({ 0.0f, 1.0f, 0.0f }, dt * 0.7f);
        return proj * view * model;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  DrawFrame  -  scheduler-driven
    // ─────────────────────────────────────────────────────────────────────────
    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);

        Frame& f = scheduler.BeginFrame();

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR)
        {
            scheduler.EndFrame();
            return;
        }
        (void)acq;

        VkClearValue clear{};
        clear.color = { { 0.08f, 0.09f, 0.11f, 1.0f } };

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

        VkRect2D scissor{};
        scissor.extent = swapchain.GetExtent();
        vkCmdSetScissor(f.PrimaryCmd(), 0, 1, &scissor);

        vkCmdBindPipeline(f.PrimaryCmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());

        // V C K  p u s h  c o n s t a n t s  ────────────────────────────────
        pc.Set("mvp", ComputeMVP());
        pc.Apply(f.PrimaryCmd(), pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT);

        mesh.RecordDraw(f.PrimaryCmd());

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

        (void)vkQueuePresentKHR(device.GetPresentQueue(), &present);
    }

    void OnWindowRefresh() { DrawFrame(); }

    // =========================================================================
    //  Init
    // =========================================================================
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

        int fbw = 0, fbh = 0;
        window.GetFramebufferSize(fbw, fbh);
        swapchain.Initialize(device, context, fbw, fbh);

        shaders.VertexSpirv   = LoadSpv("./assets/easycube.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/easycube.frag.spv");

        // V C K : : V e r t e x L a y o u t  ─────────────────────────────────
        vlayout.Add("position", VertexAttrType::Vec3)
               .Add("normal",   VertexAttrType::Vec3);

        vertexInput.Bindings   = { vlayout.Binding(0) };
        vertexInput.Attributes = vlayout.Attributes(0);

        // V C K : : P u s h C o n s t a n t s  ───────────────────────────────
        pc.Declare("mvp", PushConstType::Mat4);

        VulkanPipeline::Config pcfg;
        pcfg.pushConstantRanges = { pc.Range(VK_SHADER_STAGE_VERTEX_BIT) };
        pcfg.cullMode           = VK_CULL_MODE_BACK_BIT;
        pcfg.frontFace          = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        pipeline.Initialize(device, swapchain, shaders, vertexInput, pcfg);
        command.Initialize(device);
        sync.Initialize(device);
        framebuffers.Initialize(device, swapchain, pipeline);

        FrameScheduler::Config fcfg;
        fcfg.policy         = FramePolicy::Pipelined;
        fcfg.enableTimeline = false;
        scheduler.Initialize(device, command, sync, fcfg);

        // V C K : : P r i m i t i v e s : : C u b e  ─────────────────────────
        Primitives::Mesh cpu = Primitives::Cube(1.0f);

        g_Verts.reserve(cpu.positions.size());
        for (std::size_t i = 0; i < cpu.positions.size(); ++i)
        {
            V v;
            v.position = cpu.positions[i];
            v.normal   = cpu.normals[i];
            g_Verts.push_back(v);
        }
        g_Indices = cpu.indices;

        // VulkanMesh::Upload's `vertexSize` is the TOTAL byte count of the
        // vertex buffer (staging.CreateStaging + staging.Upload use it
        // directly), not the per-vertex stride.  sizeof(V) alone would upload
        // a single vertex's worth of bytes and the cube would render garbage.
        mesh.Upload(device, command,
                    g_Verts.data(),
                    static_cast<VkDeviceSize>(sizeof(V) * g_Verts.size()),
                    static_cast<uint32_t>(g_Verts.size()),
                    g_Indices.data(),
                    static_cast<uint32_t>(g_Indices.size()));
    }

    // =========================================================================
    //  Shutdown
    // =========================================================================
    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());

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

    // =========================================================================
    //  Run
    // =========================================================================
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

} // namespace VCK::EasyCubeExample
