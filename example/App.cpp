#include <cstring>
#include "App.h"
#include "vulkan/VulkanModule.h"
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <array>

namespace GTA_Sandbox::App {

    // ─────────────────────────────────────────────────────────────────────────
    //  Window
    // ─────────────────────────────────────────────────────────────────────────
    std::string title         = "Example of VulkanBaseplate with GLFW";
    GLFWwindow* window        = nullptr;
    int         window_width  = 1280;
    int         window_height = 720;

    // ─────────────────────────────────────────────────────────────────────────
    //  GPU structs — std140, must match model.vert / model_vertcolor.frag
    // ─────────────────────────────────────────────────────────────────────────

    struct alignas(16) CameraUBOData   // set 0, binding 0
    {
        float view[16];
        float proj[16];
        float viewProj[16];
        float camPos[3];
        float _pad;
    };

    struct alignas(16) SceneParamsData  // set 0, binding 1
    {
        float u_amb[4];
        float u_emiss[4];
        float u_matColor[4];
        float u_surfProps[4];
        float u_colorscale[4];
        float u_fogColor[4];
        float u_fogStart;
        float u_fogEnd;
        int   u_debugMode;
        float _pad;
    };

    struct ModelPC  // push constant, 128 bytes
    {
        float model[16];
        float normalMatrix[16];
    };

    // Vertex layout must match model.vert attribute locations 0-3
    struct Vertex {
        float position[3];  // location 0
        float normal[3];    // location 1
        float uv[2];        // location 2
        float color[4];     // location 3
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Vulkan objects
    //  Init:     Context → Device → Swapchain → Pipeline → Command → Sync
    //  Shutdown: reverse, expansion objects first
    // ─────────────────────────────────────────────────────────────────────────
    VulkanContext             context;
    VulkanDevice              device;
    VulkanSwapchain           swapchain;
    VulkanPipeline            pipeline;       // owns the VkRenderPass
    VulkanCommand             command;
    VulkanSync                sync;

    VulkanModelPipeline       modelPipeline;  // real draw pipeline (UBOs + push constants)
    VulkanFramebufferSet      framebuffers;
    VulkanMesh                mesh;
    VulkanDescriptorAllocator descAllocator;
    VulkanTexture             dummyTexture;   // 1x1 white — satisfies set 1 sampler binding

    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> set0Sets{};
    VkDescriptorSet                                   set1Set = VK_NULL_HANDLE;

    std::array<VulkanBuffer, MAX_FRAMES_IN_FLIGHT> camBuffers{};
    std::array<VulkanBuffer, MAX_FRAMES_IN_FLIGHT> sceneBuffers{};

    VulkanPipeline::ShaderInfo      shaders;
    VulkanPipeline::VertexInputInfo vertexInput;

    bool g_Resized   = false;
    bool g_Minimized = false;

    // ─────────────────────────────────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────────────────────────────────
    void OnFramebufferResize(GLFWwindow*, int w, int h)
    {
        window_width  = w;
        window_height = h;
        if (w == 0 || h == 0) { g_Minimized = true; return; }
        g_Minimized = false;
        g_Resized   = true;
    }

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
        framebuffers.Recreate(pipeline.GetRenderPass());
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  DrawFrame
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

        uint32_t frame = sync.GetCurrentFrameIndex();

        // ── Upload UBOs ───────────────────────────────────────────────────────
        static const float kI[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

        CameraUBOData cam{};
        std::memcpy(cam.view,     kI, 64);
        std::memcpy(cam.proj,     kI, 64);
        std::memcpy(cam.viewProj, kI, 64);
        camBuffers[frame].Upload(&cam, sizeof(cam));

        SceneParamsData scene{};
        scene.u_matColor[0]   = scene.u_matColor[1]   = scene.u_matColor[2]   = scene.u_matColor[3]   = 1.f;
        scene.u_colorscale[0] = scene.u_colorscale[1] = scene.u_colorscale[2] = scene.u_colorscale[3] = 1.f;
        scene.u_fogStart      = 100.f;
        scene.u_fogEnd        = 200.f;
        sceneBuffers[frame].Upload(&scene, sizeof(scene));

        // ── Acquire ───────────────────────────────────────────────────────────
        VkFence     fence      = sync.GetInFlightFence(frame);
        VkSemaphore imageReady = sync.GetImageAvailableSemaphore(frame);
        vkWaitForFences(device.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device.GetDevice(), swapchain.GetSwapchain(),
                                              UINT64_MAX, imageReady, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { HandleResize(); return; }
        if (acq == VK_SUBOPTIMAL_KHR)          g_Resized = true;

        vkResetFences(device.GetDevice(), 1, &fence);

        // ── Record ────────────────────────────────────────────────────────────
        command.BeginRecording(frame);
        VkCommandBuffer cmd = command.GetCommandBuffer(frame);

        VkClearValue clear{};
        clear.color = { {0.1f, 0.1f, 0.15f, 1.0f} };

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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline.GetPipeline());

        std::array<VkDescriptorSet, 2> sets = { set0Sets[frame], set1Set };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            modelPipeline.GetPipelineLayout(), 0,
            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

        ModelPC pc{};
        std::memcpy(pc.model,        kI, 64);
        std::memcpy(pc.normalMatrix, kI, 64);
        vkCmdPushConstants(cmd, modelPipeline.GetPipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPC), &pc);

        mesh.RecordDraw(cmd);

        vkCmdEndRenderPass(cmd);
        command.EndRecording(frame);

        // ── Submit ────────────────────────────────────────────────────────────
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore          signalSem = sync.GetRenderFinishedSemaphore(frame);
        VkCommandBuffer      cmdBuf   = command.GetCommandBuffer(frame);

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

        // ── Present ───────────────────────────────────────────────────────────
        VkSwapchainKHR sc = swapchain.GetSwapchain();

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

        sync.AdvanceFrame();
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
        device.Initialize(context.GetInstance(), context.GetSurface());
        swapchain.Initialize(device, context.GetSurface(), window_width, window_height);

        shaders.VertexSpirv   = LoadSpv("../assets/model.vert.spv");
        shaders.FragmentSpirv = LoadSpv("../assets/model_vertcolor.frag.spv");

        vertexInput.Bindings = {{
            .binding   = 0,
            .stride    = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        }};
        vertexInput.Attributes = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(Vertex, position) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(Vertex, normal)   },
            { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,       .offset = offsetof(Vertex, uv)       },
            { .location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, color)    },
        };

        pipeline.Initialize(device, swapchain.GetImageFormat(), shaders, vertexInput);
        command.Initialize(device);
        sync.Initialize(device);
        modelPipeline.Initialize(device, pipeline.GetRenderPass(), shaders, vertexInput);
        framebuffers.Initialize(device, swapchain, pipeline.GetRenderPass());

        // ── Descriptor pool: 2 UBOs/frame + 1 combined sampler ───────────────
        descAllocator.Initialize(device,
            MAX_FRAMES_IN_FLIGHT + 1,
            {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         MAX_FRAMES_IN_FLIGHT * 2 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1                        },
            });

        // ── Per-frame UBO buffers + set 0 ────────────────────────────────────
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            camBuffers[i].CreateUniform(device,   sizeof(CameraUBOData));
            sceneBuffers[i].CreateUniform(device, sizeof(SceneParamsData));

            set0Sets[i] = descAllocator.Allocate(modelPipeline.GetSet0Layout());

            VkDescriptorBufferInfo camInfo{};
            camInfo.buffer = camBuffers[i].GetBuffer();
            camInfo.range  = sizeof(CameraUBOData);

            VkDescriptorBufferInfo sceneInfo{};
            sceneInfo.buffer = sceneBuffers[i].GetBuffer();
            sceneInfo.range  = sizeof(SceneParamsData);

            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = set0Sets[i];
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo     = &camInfo;

            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = set0Sets[i];
            writes[1].dstBinding      = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].pBufferInfo     = &sceneInfo;

            vkUpdateDescriptorSets(device.GetDevice(),
                static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        // ── Dummy 1x1 white texture for set 1 sampler binding ────────────────
        const uint8_t white[4] = { 255, 255, 255, 255 };
        dummyTexture.CreateFromPixels(device, command, white, 1, 1);

        set1Set = descAllocator.Allocate(modelPipeline.GetSet1Layout());

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = dummyTexture.GetSampler();
        imgInfo.imageView   = dummyTexture.GetImageView();
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet imgWrite{};
        imgWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imgWrite.dstSet          = set1Set;
        imgWrite.dstBinding      = 0;
        imgWrite.descriptorCount = 1;
        imgWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imgWrite.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(device.GetDevice(), 1, &imgWrite, 0, nullptr);

        // ── RGB triangle centered at NDC (0, 0) ───────────────────────────────
        //  CCW winding (top → bottom-left → bottom-right) for VK_FRONT_FACE_COUNTER_CLOCKWISE.
        //  normal/uv are unused by model_vertcolor.frag but required by the vertex layout.
        const std::vector<Vertex> vertices = {
            {{ 0.0f,  -0.6667f, 0.f}, {0,0,1}, {0.5f,0.f}, {1,0,0,1}},  // top          — red
            {{-0.5f,   0.3333f, 0.f}, {0,0,1}, {0.f, 1.f}, {0,0,1,1}},  // bottom-left  — blue
            {{ 0.5f,   0.3333f, 0.f}, {0,0,1}, {1.f, 1.f}, {0,1,0,1}},  // bottom-right — green
        };
        const std::vector<uint32_t> indices = { 0, 1, 2 };

        mesh.Upload(device, command,
                    vertices.data(), vertices.size() * sizeof(Vertex),
                    indices.data(),  static_cast<uint32_t>(indices.size()));
    }

    // =========================================================================
    //  Shutdown  —  reverse of Init, expansion objects first
    // =========================================================================
    void Shutdown()
    {
        vkDeviceWaitIdle(device.GetDevice());

        mesh.Shutdown();
        dummyTexture.Shutdown();
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            sceneBuffers[i].Shutdown();
            camBuffers[i].Shutdown();
        }
        descAllocator.Shutdown();
        framebuffers.Shutdown();
        modelPipeline.Shutdown();

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

} // namespace GTA_Sandbox::App
