#include "App.h"
#include "VCK.h"
#include "stb_easy_font.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>

// =============================================================================
//  HelloExample
//
//  Minimal FrameScheduler-driven program that renders "Hello World" in big
//  white letters centred on a dark background.
//
//  Text is built once at Init with stb_easy_font (public-domain, single-file
//  header included beside this TU).  Each glyph becomes a small batch of
//  axis-aligned quads in pixel coordinates; we convert those pixel coords to
//  NDC so the text scales with the window.
//
//  Flow, per frame:
//
//      Frame& f = scheduler.BeginFrame();    // fence wait + cmd begin
//      (vkAcquireNextImageKHR)
//      (vkCmdBeginRenderPass → draw → vkCmdEndRenderPass on f.PrimaryCmd())
//      f.Submissions().QueueGraphics(f.PrimaryCmd(), submitInfo);
//      scheduler.EndFrame();                 // cmd end + flush submits
//      (vkQueuePresentKHR)
// =============================================================================

namespace VCK::HelloExample {

    // ─────────────────────────────────────────────────────────────────────────
    //  Window state
    // ─────────────────────────────────────────────────────────────────────────
    std::string title         = "HelloExample — Hello, World";
    GLFWwindow* window        = nullptr;
    int         window_width  = 1280;
    int         window_height = 720;

    bool g_Resized   = false;
    bool g_Minimized = false;

    // ─────────────────────────────────────────────────────────────────────────
    //  Vertex — matches the layout stb_easy_font emits (16 bytes):
    //      x,y,z    : float
    //      color[4] : uint8 (RGBA, fed to shader via R8G8B8A8_UNORM)
    // ─────────────────────────────────────────────────────────────────────────
    struct Vertex {
        float   position[3];
        uint8_t color[4];
    };
    static_assert(sizeof(Vertex) == 16, "stb_easy_font layout mismatch");

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

    void HandleResize()
    {
        if (window_width == 0 || window_height == 0) return;
        vkDeviceWaitIdle(device.GetDevice());
        swapchain.Recreate(window_width, window_height);
        framebuffers.Recreate(pipeline);
    }

    void OnFramebufferResize(GLFWwindow*, int w, int h)
    {
        window_width  = w;
        window_height = h;
        if (w == 0 || h == 0) { g_Minimized = true; return; }
        g_Minimized = false;
        g_Resized   = true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  DrawFrame  —  scheduler-driven
    // ─────────────────────────────────────────────────────────────────────────
    void DrawFrame()
    {
        if (g_Minimized || window_width == 0 || window_height == 0) return;

        if (g_Resized)
        {
            g_Resized = false;
            HandleResize();
            if (window_width == 0 || window_height == 0) return;
        }

        // ── Scheduler takes over the fence wait + cmd.BeginRecording ──────────
        Frame& f = scheduler.BeginFrame();

        // ── Acquire (scheduler does not do this — different sema semantics) ──
        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                             UINT64_MAX, f.ImageAvailable(),
                                             VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // Cancel this frame cleanly — record an empty pass so the cmd
            // buffer is well-formed, then flush + resize.
            scheduler.EndFrame();
            HandleResize();
            return;
        }
        if (acq == VK_SUBOPTIMAL_KHR) g_Resized = true;

        // ── Record the render pass into the scheduler's primary cmd ──────────
        VkClearValue clear{};
        clear.color = { {0.06f, 0.07f, 0.09f, 1.0f} };

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
        mesh.RecordDraw(f.PrimaryCmd());

        vkCmdEndRenderPass(f.PrimaryCmd());

        // ── Queue the primary cmd into the batcher ────────────────────────────
        GpuSubmissionBatcher::SubmitInfo si;
        si.waitSem   = f.ImageAvailable();
        si.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.signalSem = f.RenderFinished();
        f.Submissions().QueueGraphics(f.PrimaryCmd(), si);

        // ── Scheduler closes the cmd + flushes the batch with the in-flight fence ──
        scheduler.EndFrame();

        // ── Present ───────────────────────────────────────────────────────────
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
        if (pres == VK_ERROR_OUT_OF_DATE_KHR) HandleResize();
        else if (pres == VK_SUBOPTIMAL_KHR)   g_Resized = true;
    }

    void OnWindowRefresh(GLFWwindow*) { DrawFrame(); }

    // =========================================================================
    //  Init
    // =========================================================================
    void Init()
    {
        if (!glfwInit()) return;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

        window = glfwCreateWindow(window_width, window_height, title.c_str(), nullptr, nullptr);
        if (!window) { glfwTerminate(); return; }

        glfwSetFramebufferSizeCallback(window, OnFramebufferResize);
        glfwSetWindowRefreshCallback(window,   OnWindowRefresh);

        HWND hwnd = glfwGetWin32Window(window);

        context.Initialize(hwnd, title);
        device.Initialize(context);
        swapchain.Initialize(device, context, window_width, window_height);

        shaders.VertexSpirv   = LoadSpv("./assets/hello.vert.spv");
        shaders.FragmentSpirv = LoadSpv("./assets/hello.frag.spv");

        vertexInput.Bindings = {{
            .binding   = 0,
            .stride    = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM,   .offset = offsetof(Vertex, color)    },
        };

        pipeline.Initialize(device, swapchain, shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        framebuffers.Initialize(device, swapchain, pipeline);

        // Scheduler: Pipelined policy (the default).  Timeline enabled so
        // LogVk prints per-frame spans if you enable Dump().
        FrameScheduler::Config cfg;
        cfg.policy         = FramePolicy::Pipelined;
        cfg.enableTimeline = false;     // flip to true + call scheduler.Timeline().Dump() to see spans
        scheduler.Initialize(device, command, sync, cfg);

        // --------------------------------------------------------------
        //  Build the "Hello, World" mesh.
        //
        //  1. stb_easy_font_print emits 4 vertices per character quad in
        //     pixel space (x rightward, y DOWNWARD, z=0, u8 RGBA).
        //  2. We compute scale + offset so the text occupies ~70% of NDC
        //     horizontally and is centred vertically.  The text STRETCHES
        //     with the window -- perfect for a "Hello World" demo.
        //  3. Quads are converted to triangle-list indices (2 triangles /
        //     4 vertices per quad, 6 indices per quad).
        // --------------------------------------------------------------
        const char* kText = "Hello, World";

        // stb doc: ~270 bytes per char upper bound.  Reserve plenty.
        std::vector<uint8_t> raw(std::strlen(kText) * 320 + 64);
        int numQuads = stb_easy_font_print(
            /*x=*/0.0f, /*y=*/0.0f,
            const_cast<char*>(kText),
            /*color=*/nullptr,              // default opaque white
            raw.data(), static_cast<int>(raw.size()));

        const int textW = stb_easy_font_width (const_cast<char*>(kText));
        const int textH = stb_easy_font_height(const_cast<char*>(kText));

        const float kTargetNdc = 1.4f;          // text width in NDC units (full span = 2.0)
        const float scale      = kTargetNdc / static_cast<float>(textW);
        const float offsetX    = -(textW * scale) * 0.5f;
        const float offsetY    = -(textH * scale) * 0.5f;

        const int numVerts = numQuads * 4;
        std::vector<Vertex> vertices(numVerts);
        for (int i = 0; i < numVerts; ++i)
        {
            Vertex v;
            std::memcpy(&v, raw.data() + i * sizeof(Vertex), sizeof(Vertex));
            v.position[0] = v.position[0] * scale + offsetX;
            v.position[1] = v.position[1] * scale + offsetY;
            v.position[2] = 0.0f;
            vertices[i] = v;
        }

        // Quads -> triangle-list indices.
        //
        // stb_easy_font emits vertices in order TL, TR, BR, BL (pixel space,
        // y-down).  The VCK default pipeline uses VK_FRONT_FACE_COUNTER_CLOCKWISE
        // with back-face culling; under Vulkan's y-down framebuffer convention
        // that means triangles with NEGATIVE signed area are front-facing.
        // So we want visually-CW-on-screen windings: (TL, BR, TR) + (TL, BL, BR)
        // i.e. (0,2,1) + (0,3,2).  RGBTriangle uses the same visually-CW ordering.
        std::vector<uint32_t> indices;
        indices.reserve(numQuads * 6);
        for (int q = 0; q < numQuads; ++q)
        {
            const uint32_t b = static_cast<uint32_t>(q * 4);
            indices.push_back(b + 0);
            indices.push_back(b + 2);
            indices.push_back(b + 1);
            indices.push_back(b + 0);
            indices.push_back(b + 3);
            indices.push_back(b + 2);
        }

        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));

        LogVk("[HelloExample] FrameScheduler running with FramePolicy::Pipelined.");
        LogVk(std::string("[HelloExample] \"") + kText + "\" -- "
              + std::to_string(numQuads)       + " quads, "
              + std::to_string(numVerts)       + " verts, "
              + std::to_string(indices.size()) + " indices.");
    }

    // =========================================================================
    //  Shutdown  —  scheduler first, then expansion objects, then core
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

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    // =========================================================================
    //  Run
    // =========================================================================
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

} // namespace VCK::HelloExample
