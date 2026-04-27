#include "App.h"
#include "VCK.h"
#include <chrono>
#include <cstdint>
#include <vector>

// =============================================================================
//  ShaderToolingExample  -  implementation
//
//  Init order is the same as every scheduler-driven example - what changes
//  is everything around the pipeline call:
//
//      ShaderLoader  ->  loads the .spv pair so the App.cpp never opens a
//                        file by hand.
//      ShaderStage   ->  declares VertexLayout + PushConstants for the
//                        vertex stage in one place; the fragment stage
//                        gets its own (currently empty) declaration.
//      SpecConstants ->  pins fragment-stage spec const 0 = 0.85f.
//      ShaderInterface ->  combines the stages, hands back VertexInput()
//                          + a PipelineConfig pre-filled with the
//                          push-constant range; ApplyToConfig() wires
//                          the SpecConstants in.
//      ShaderWatcher ->  debug-only.  Polls the .spv timestamps each
//                        frame; when one changes the example calls
//                        scheduler.DrainInFlight + pipeline.Reinitialize
//                        + framebuffers.Recreate.  Touch
//                        assets/shadertool.frag.spv while running and
//                        the brightness flips.
//
//  DrawFrame:
//      compute MVP -> pc.Set("mvp", mvp).
//      BeginFrame -> [hot reload check] -> acquire -> BeginRenderPass ->
//      BindPipeline -> pc.Apply -> mesh.RecordDraw -> EndRenderPass ->
//      QueueGraphics -> EndFrame -> vkQueuePresentKHR.
// =============================================================================

namespace VCK::ShaderToolingExample {

    // ─────────────────────────────────────────────────────────────────────────
    //  Window state
    // ─────────────────────────────────────────────────────────────────────────
    std::string title = "ShaderToolingExample - ShaderLoader/Watcher/SpecConstants/Stage/Interface";
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

    // The five v0.4 classes the example exercises.
    ShaderLoader   loader;
    ShaderWatcher  watcher;          // debug-only - populated when cfg.debug
    SpecConstants  fragSpec;
    // Stages live behind the ShaderInterface; we keep the ShaderStage
    // copies here only to make the construction read top-to-bottom.

    // The cached pipeline-creation inputs.  PipelineConfig() is rebuilt on
    // hot-reload so we keep the source ShaderInterface alive for that path.
    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;
    VulkanPipeline::Config          pcfg;

    // Single push constant block reused by ShaderStage::Push() and the
    // per-frame Apply call.  Lives on the ShaderInterface; we grab a
    // reference once Init has built it.
    PushConstants* pc = nullptr;

    // Interleaved triangle vertices: { Vec3 position; Vec3 color }.
    struct V { Vec3 position; Vec3 color; };
    static_assert(sizeof(V) == 24, "Vec3 must be tightly packed");

    std::vector<V>        g_Verts;
    std::vector<uint32_t> g_Indices;

    auto g_StartTime = std::chrono::steady_clock::now();

    bool g_Debug = true;            // mirrors cfg.debug; set in Init
    static const char* kVertSpv = "./assets/shadertool.vert.spv";
    static const char* kFragSpv = "./assets/shadertool.frag.spv";

    // ─────────────────────────────────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────────────────────────────────
    Mat4 ComputeMVP()
    {
        const float dt = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - g_StartTime).count();

        const uint32_t w = swapchain.GetExtent().width;
        const uint32_t h = swapchain.GetExtent().height;
        const float aspect = (h == 0) ? 1.0f
                           : static_cast<float>(w) / static_cast<float>(h);

        Mat4 proj  = Perspective(Radians(60.0f), aspect, 0.1f, 100.0f);
        Mat4 view  = LookAt({ 0.0f, 0.0f, 2.5f },
                            { 0.0f, 0.0f, 0.0f },
                            { 0.0f, 1.0f, 0.0f });
        Mat4 model = Rotate({ 0.0f, 1.0f, 0.0f }, dt * 0.6f);
        return proj * view * model;
    }

    // Re-runs the shader-tooling pipeline build using whatever SPIR-V the
    // ShaderLoader (or, on hot reload, the ShaderWatcher) currently holds.
    // Caller is responsible for scheduler.DrainInFlight before invoking
    // this so no in-flight cmd buffer references the old pipeline.
    void RebuildPipeline(const VulkanPipeline::ShaderInfo& info)
    {
        pipeline.Shutdown();
        pipeline.Initialize(device, swapchain, info, vertexInput, pcfg);
        framebuffers.Recreate(pipeline);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  DrawFrame  -  scheduler-driven
    // ─────────────────────────────────────────────────────────────────────────
    void DrawFrame()
    {
        if (window.IsMinimized()) return;

        VCK::HandleLiveResize(window, device, swapchain, framebuffers, pipeline);

        // Debug-only hot reload.  HasChanged() is one stat() per watched
        // file; the branch is free in release because watcher.m_Files is
        // empty (Watch() is never called when g_Debug is false).
        if (g_Debug && watcher.HasChanged())
        {
            VCKLog::Notice("ShaderToolingExample", "hot reloading...");
            scheduler.DrainInFlight();
            if (watcher.Reload())
            {
                RebuildPipeline(watcher.GetShaderInfo());
            }
            watcher.ResetChanged();
        }

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
        clear.color = { { 0.07f, 0.08f, 0.10f, 1.0f } };

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

        pc->Set("mvp", ComputeMVP());
        pc->Apply(f.PrimaryCmd(), pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT);

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

    // The ShaderInterface lives at function scope in Init() but VertexInput()
    // and PipelineConfig() return by value, so we capture them into the file
    // statics above before the interface goes out of scope.  Push() returns
    // a reference into the interface, so the interface itself has to outlive
    // the frame loop - we stash it here.
    static ShaderInterface* g_Iface = nullptr;

    void Init()
    {
        VCK::WindowCreateInfo wci;
        wci.width     = g_InitW;
        wci.height    = g_InitH;
        wci.title     = title;
        wci.resizable = true;
        if (!window.Create(wci)) return;
        window.SetWindowRefreshCallback(OnWindowRefresh);

        VCK::Config cfg;
        cfg.debug = g_Debug;            // single source of truth for the
                                        // hot-reload branch + Info logs
        VCKLog::SetDebug(cfg.debug);

        context.Initialize(window, title);
        device.Initialize(context);

        int fbw = 0, fbh = 0;
        window.GetFramebufferSize(fbw, fbh);
        swapchain.Initialize(device, context, fbw, fbh);

        // [26]  S h a d e r L o a d e r  ──────────────────────────────────────
        if (!loader.LoadFromFile(kVertSpv, VK_SHADER_STAGE_VERTEX_BIT) ||
            !loader.LoadFromFile(kFragSpv, VK_SHADER_STAGE_FRAGMENT_BIT))
        {
            // R14: every failure already logged inside ShaderLoader.
            return;
        }
        shaders = loader.GetShaderInfo();

        // [27]  S h a d e r W a t c h e r  ────────────────────────────────────
        // Debug-only - in release the watcher stays empty and HasChanged()
        // is a no-op (R19 zero cost when nothing's registered).
        if (g_Debug)
        {
            watcher.Watch(kVertSpv, VK_SHADER_STAGE_VERTEX_BIT);
            watcher.Watch(kFragSpv, VK_SHADER_STAGE_FRAGMENT_BIT);
            watcher.Reload();           // seed the watcher's SPIR-V cache
        }

        // [28]  S p e c C o n s t a n t s  ────────────────────────────────────
        // Pin fragment-stage constant_id 0 (BRIGHTNESS) to 0.85.
        fragSpec.Set(0u, 0.85f);

        // [29] [30]  S h a d e r S t a g e  +  S h a d e r I n t e r f a c e  ─
        // One vertex stage with the layout + push block, one fragment stage
        // (empty - no vertex inputs, no push, no descriptors), merged into
        // a ShaderInterface that hands the pipeline everything it needs.
        ShaderStage vs(VK_SHADER_STAGE_VERTEX_BIT);
        vs.Vertex().Add("position", VertexAttrType::Vec3)
                   .Add("color",    VertexAttrType::Vec3);
        vs.Push().Declare("mvp", PushConstType::Mat4);

        ShaderStage fs(VK_SHADER_STAGE_FRAGMENT_BIT);
        // No descriptor bindings; spec constants are wired through Config,
        // not through the stage decl.

        static ShaderInterface iface{ vs, fs };
        g_Iface = &iface;

        vertexInput = iface.VertexInput();
        pcfg        = iface.PipelineConfig();
        pcfg.cullMode  = VK_CULL_MODE_NONE;
        pcfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pcfg.fragSpecialization = fragSpec.GetInfo();
        pc          = &iface.Push();

        // Demonstrate BuildSetLayout even though this example has no
        // descriptors - the call returns a layout with zero bindings,
        // which we destroy immediately to keep the example self-contained.
        VkDescriptorSetLayout dummySet = iface.BuildSetLayout(device, 0);
        if (dummySet != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device.GetDevice(), dummySet, nullptr);

        pipeline.Initialize(device, swapchain, shaders, vertexInput, pcfg);
        command.Initialize(device);
        sync.Initialize(device);
        framebuffers.Initialize(device, swapchain, pipeline);

        FrameScheduler::Config fcfg;
        fcfg.policy         = FramePolicy::Pipelined;
        fcfg.enableTimeline = false;
        scheduler.Initialize(device, command, sync, fcfg);

        // Triangle in NDC-ish space; the MVP rotates it.
        g_Verts = {
            { { 0.0f,  0.6f, 0.0f }, { 1.0f, 0.2f, 0.2f } },
            { {-0.6f, -0.4f, 0.0f }, { 0.2f, 1.0f, 0.2f } },
            { { 0.6f, -0.4f, 0.0f }, { 0.2f, 0.4f, 1.0f } },
        };
        g_Indices = { 0, 1, 2 };

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

        // Pure-CPU classes - no Vulkan handles, just clear their state.
        watcher.Shutdown();
        loader.Clear();
        fragSpec.Clear();

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

} // namespace VCK::ShaderToolingExample
