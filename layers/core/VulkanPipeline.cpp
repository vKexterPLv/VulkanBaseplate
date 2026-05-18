#include "VulkanPipeline.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include <array>

namespace VCK {

    // ─────────────────────────────────────────────────────────────────────────────
    // Preferred overload - pulls swapchain colour format AND MSAA sample
    // count out of the swapchain so the caller does not have to thread them
    // through manually.
    bool VulkanPipeline::Initialize(VulkanDevice&          device,
                                    VulkanSwapchain&       swapchain,
                                    const ShaderInfo&      shaders,
                                    const VertexInputInfo& vertexInput)
    {
        return Initialize(device, swapchain.GetImageFormat(), shaders, vertexInput,
                          swapchain.GetMSAASamples());
    }

    bool VulkanPipeline::Initialize(VulkanDevice&          device,
                                    VulkanSwapchain&       swapchain,
                                    const ShaderInfo&      shaders,
                                    const VertexInputInfo& vertexInput,
                                    const Config&          pipelineConfig)
    {
        return Initialize(device, swapchain.GetImageFormat(), shaders, vertexInput,
                          swapchain.GetMSAASamples(), pipelineConfig);
    }

    bool VulkanPipeline::Initialize(VulkanDevice& device,
        VkFormat               swapchainFormat,
        const ShaderInfo& shaders,
        const VertexInputInfo& vertexInput,
        VkSampleCountFlagBits  samples)
    {
        // Zero-config forwarder: matches the pre-Config behaviour.
        return Initialize(device, swapchainFormat, shaders, vertexInput, samples, Config{});
    }

    bool VulkanPipeline::Initialize(VulkanDevice&          device,
        VkFormat               swapchainFormat,
        const ShaderInfo&      shaders,
        const VertexInputInfo& vertexInput,
        VkSampleCountFlagBits  samples,
        const Config&          pipelineConfig)
    {
        m_Device      = &device;
        m_PipelineCfg = pipelineConfig;

        // Theme E (E1): pick the render-pass codepath up-front.  When the
        // device successfully enabled VK_KHR_dynamic_rendering at Initialize
        // (cfg.rendering.mode = Dynamic AND the feature was available), this
        // pipeline skips CreateRenderPass and instead chains
        // VkPipelineRenderingCreateInfo into vkCreateGraphicsPipelines.
        // Otherwise the legacy VkRenderPass + VkFramebuffer codepath is used
        // verbatim (behaviour unchanged for Classic mode and for devices that
        // did not advertise the feature).
        m_UsesDynamicRendering = device.HasDynamicRendering();
        m_DynamicColorFormat   = m_UsesDynamicRendering ? swapchainFormat : VK_FORMAT_UNDEFINED;

        // MSAA end-to-end is wired:
        //   - VulkanSwapchain owns one multisampled colour target per image.
        //   - Render pass below adds a resolve attachment when samples > 1.
        //   - VulkanFramebufferSet binds [MSAA view, swapchain view].
        // Caller still supplies the sample count via the swapchain (see
        // cfg.swapchain.msaaSamples); we simply mirror it here.
        m_Samples = samples;

        // Per-step Info breadcrumbs at the public-API boundary so a reader
        // scanning the log can tell at a glance which sub-stage was running
        // when Initialize returned false.  Each sub-function owns its
        // subsystem-tagged Error on failure (R14: exactly one Error per
        // failure path) - these Info lines do not log on the failure branch.
        //
        // Failure paths roll back already-built sub-state inline AND drop
        // the device pointer so the object lands in default-construct-
        // equivalent state - a subsequent Shutdown() then hits the
        // `if (!m_Device) return;` guard and stays silent (R19), instead
        // of emitting a misleading 'Pipeline Shut down' line for an
        // object whose Initialize never finished.  Matches the VulkanDevice
        // / VulkanCommand / VulkanSync rollback contract.
        VkDevice rawDevice = m_Device->GetDevice();
        const auto rollbackToDefault = [&]() {
            m_Device               = nullptr;
            m_PipelineCfg          = Config{};
            m_Samples              = VK_SAMPLE_COUNT_1_BIT;
            m_UsesDynamicRendering = false;
            m_DynamicColorFormat   = VK_FORMAT_UNDEFINED;
        };

        if (m_UsesDynamicRendering)
        {
            // Dynamic rendering: no VkRenderPass to create.  m_RenderPass
            // stays VK_NULL_HANDLE and CreateGraphicsPipeline below chains
            // VkPipelineRenderingCreateInfo with the swapchain colour format.
            VCKLog::Info("Pipeline", "Skipping render pass (dynamic rendering)");
        }
        else
        {
            VCKLog::Info("Pipeline", "Creating render pass...");
            if (!CreateRenderPass(swapchainFormat)) {
                rollbackToDefault();
                return false;
            }
        }

        VCKLog::Info("Pipeline", "Creating pipeline layout...");
        if (!CreatePipelineLayout()) {
            if (m_RenderPass) {
                vkDestroyRenderPass(rawDevice, m_RenderPass, nullptr);
                m_RenderPass = VK_NULL_HANDLE;
            }
            rollbackToDefault();
            return false;
        }

        VCKLog::Info("Pipeline", "Creating graphics pipeline...");
        if (!CreateGraphicsPipeline(shaders, vertexInput)) {
            vkDestroyPipelineLayout(rawDevice, m_PipelineLayout, nullptr);
            if (m_RenderPass) {
                vkDestroyRenderPass(rawDevice, m_RenderPass, nullptr);
                m_RenderPass = VK_NULL_HANDLE;
            }
            m_PipelineLayout = VK_NULL_HANDLE;
            rollbackToDefault();
            return false;
        }

        VCKLog::Info("Pipeline", "Initialized");
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    void VulkanPipeline::Shutdown()
    {
        if (!m_Device) return;

        VkDevice device = m_Device->GetDevice();

        if (m_Pipeline) { vkDestroyPipeline(device, m_Pipeline, nullptr); m_Pipeline = VK_NULL_HANDLE; }
        if (m_PipelineLayout) { vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr); m_PipelineLayout = VK_NULL_HANDLE; }
        if (m_RenderPass) { vkDestroyRenderPass(device, m_RenderPass, nullptr); m_RenderPass = VK_NULL_HANDLE; }

        m_Device = nullptr;
        VCKLog::Info("Pipeline", "Shut down");
    }


    // ─────────────────────────────────────────────────────────────────────────────
    //  Render pass
    //  Single colour attachment.  loadOp = CLEAR, storeOp = STORE.
    //  Subpass: colour only, layout ATTACHMENT_OPTIMAL during pass.
    //  Final layout: PRESENT_SRC_KHR - ready for vkQueuePresentKHR.
    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanPipeline::CreateRenderPass(VkFormat swapchainFormat)
    {
        const bool useMsaa = (m_Samples != VK_SAMPLE_COUNT_1_BIT);

        // Colour attachment (single-sample or multisampled depending on cfg).
        // When MSAA is on this attachment is the multisampled render target and
        // is DONT_CARE after the subpass (contents are consumed by the resolve).
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = swapchainFormat;
        colorAttachment.samples        = m_Samples;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = useMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                 : VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = useMsaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // Resolve attachment (single-sample swapchain image) - only present when
        // MSAA is enabled.  The subpass resolves the multisampled colour image
        // into this attachment before transitioning it to PRESENT_SRC_KHR.
        VkAttachmentDescription resolveAttachment{};
        resolveAttachment.format         = swapchainFormat;
        resolveAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        resolveAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference resolveAttachmentRef{};
        resolveAttachmentRef.attachment = 1;
        resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pResolveAttachments = useMsaa ? &resolveAttachmentRef : nullptr;

        // Dependency: wait for the previous present to finish writing before we
        // start writing colour again.
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        const VkAttachmentDescription attachments[2] = { colorAttachment, resolveAttachment };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = useMsaa ? 2u : 1u;
        renderPassInfo.pAttachments    = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (!VK_OK(vkCreateRenderPass(m_Device->GetDevice(), &renderPassInfo, nullptr, &m_RenderPass)))
        {
            VCKLog::Error("Pipeline",
                std::string("vkCreateRenderPass failed (") + (useMsaa ? "MSAA" : "single-sample") + ")");
            return false;
        }
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Pipeline layout
    //  No descriptor sets, no push constants yet - add them when needed.
    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanPipeline::CreatePipelineLayout()
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = static_cast<uint32_t>(m_PipelineCfg.descriptorSetLayouts.size());
        layoutInfo.pSetLayouts            = m_PipelineCfg.descriptorSetLayouts.empty()
                                          ? nullptr
                                          : m_PipelineCfg.descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(m_PipelineCfg.pushConstantRanges.size());
        layoutInfo.pPushConstantRanges    = m_PipelineCfg.pushConstantRanges.empty()
                                          ? nullptr
                                          : m_PipelineCfg.pushConstantRanges.data();

        if (!VK_OK(vkCreatePipelineLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &m_PipelineLayout)))
        {
            VCKLog::Error("Pipeline",
                "vkCreatePipelineLayout failed (setLayouts=" + std::to_string(layoutInfo.setLayoutCount)
                + ", pushRanges=" + std::to_string(layoutInfo.pushConstantRangeCount) + ")");
            return false;
        }
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    VkShaderModule VulkanPipeline::CreateShaderModule(const std::vector<uint32_t>& spirv)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        // Silent helper: the caller (CreateGraphicsPipeline) emits the
        // single subsystem-tagged Error covering both modules so that
        // failing both shader stages produces exactly one Error for the
        // user's logical failure (R14), not three.
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (!VK_OK(vkCreateShaderModule(m_Device->GetDevice(), &createInfo, nullptr, &shaderModule)))
            return VK_NULL_HANDLE;

        return shaderModule;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Graphics pipeline
    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanPipeline::CreateGraphicsPipeline(const ShaderInfo& shaders,
        const VertexInputInfo& vertexInput)
    {
        // ── Shader stages ─────────────────────────────────────────────────────────
        VkShaderModule vertModule = CreateShaderModule(shaders.VertexSpirv);
        VkShaderModule fragModule = CreateShaderModule(shaders.FragmentSpirv);

        if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
        {
            const bool vertMissing = (vertModule == VK_NULL_HANDLE);
            const bool fragMissing = (fragModule == VK_NULL_HANDLE);
            const char* which =
                (vertMissing && fragMissing) ? "vertex+fragment" :
                vertMissing                  ? "vertex" :
                                               "fragment";
            VCKLog::Error("Pipeline",
                std::string("graphics pipeline aborted: ")
                + which + " shader module(s) unavailable");
            if (vertModule) vkDestroyShaderModule(m_Device->GetDevice(), vertModule, nullptr);
            if (fragModule) vkDestroyShaderModule(m_Device->GetDevice(), fragModule, nullptr);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName = "main";
        shaderStages[0].pSpecializationInfo = m_PipelineCfg.vertSpecialization;

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName = "main";
        shaderStages[1].pSpecializationInfo = m_PipelineCfg.fragSpecialization;

        // ── Vertex input ──────────────────────────────────────────────────────────
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInput.Bindings.size());
        vertexInputInfo.pVertexBindingDescriptions = vertexInput.Bindings.empty() ? nullptr : vertexInput.Bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInput.Attributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = vertexInput.Attributes.empty() ? nullptr : vertexInput.Attributes.data();

        // ── Input assembly ────────────────────────────────────────────────────────
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = m_PipelineCfg.topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // ── Viewport + scissor (dynamic - set at draw time) ───────────────────────
        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        // pViewports / pScissors left null - filled by vkCmdSetViewport/Scissor

        // ── Rasterizer ────────────────────────────────────────────────────────────
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = m_PipelineCfg.polygonMode;
        rasterizer.lineWidth   = m_PipelineCfg.lineWidth;
        rasterizer.cullMode    = m_PipelineCfg.cullMode;
        rasterizer.frontFace   = m_PipelineCfg.frontFace;
        rasterizer.depthBiasEnable = VK_FALSE;

        // ── Multisampling ─────────────────────────────────────────────────────────
        // rasterizationSamples is driven by cfg.swapchain.msaaSamples via the
        // Initialize overloads.  A2C + sample-rate-shading are per-pipeline
        // knobs that only take effect when samples > 1 (Vulkan spec makes
        // them no-ops at 1x; we still guard them so cfg = true is safe).
        const bool msaaActive = (m_Samples != VK_SAMPLE_COUNT_1_BIT);

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples  = m_Samples;
        multisampling.sampleShadingEnable   =
            (m_PipelineCfg.sampleRateShading && msaaActive) ? VK_TRUE : VK_FALSE;
        multisampling.minSampleShading      =
            (m_PipelineCfg.sampleRateShading && msaaActive)
                ? m_PipelineCfg.minSampleShadingRate
                : 1.0f;
        multisampling.alphaToCoverageEnable =
            (m_PipelineCfg.alphaToCoverage && msaaActive) ? VK_TRUE : VK_FALSE;

        // ── Depth / stencil (disabled - add when needed) ──────────────────────────
        // Pass nullptr for pDepthStencilState when no depth attachment exists.

        // ── Colour blend - standard alpha blend ──────────────────────────────────
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask      = m_PipelineCfg.colorWriteMask;
        colorBlendAttachment.blendEnable         = m_PipelineCfg.blendEnable ? VK_TRUE : VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = m_PipelineCfg.srcColorBlendFactor;
        colorBlendAttachment.dstColorBlendFactor = m_PipelineCfg.dstColorBlendFactor;
        colorBlendAttachment.colorBlendOp        = m_PipelineCfg.colorBlendOp;
        colorBlendAttachment.srcAlphaBlendFactor = m_PipelineCfg.srcAlphaBlendFactor;
        colorBlendAttachment.dstAlphaBlendFactor = m_PipelineCfg.dstAlphaBlendFactor;
        colorBlendAttachment.alphaBlendOp        = m_PipelineCfg.alphaBlendOp;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // ── Assemble ──────────────────────────────────────────────────────────────
        // Theme E (E1): when m_UsesDynamicRendering is true, renderPass MUST be
        // VK_NULL_HANDLE and a VkPipelineRenderingCreateInfo describing the
        // colour-attachment formats is chained into pNext.  Vulkan spec §10.3.
        // Single colour attachment matching the swapchain format - depth /
        // stencil are left UNDEFINED to match the Classic codepath which
        // does not have a depth attachment yet.
        VkPipelineRenderingCreateInfo dynRenderingInfo{};
        VkFormat                      dynColorFormat = m_DynamicColorFormat;
        if (m_UsesDynamicRendering)
        {
            dynRenderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            dynRenderingInfo.colorAttachmentCount    = 1;
            dynRenderingInfo.pColorAttachmentFormats = &dynColorFormat;
            dynRenderingInfo.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
            dynRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = m_UsesDynamicRendering ? &dynRenderingInfo : nullptr;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr;          // no depth yet
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = m_RenderPass;             // VK_NULL_HANDLE in Dynamic mode
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        bool success = VK_OK(vkCreateGraphicsPipelines(
            m_Device->GetDevice(),
            VK_NULL_HANDLE,   // no pipeline cache yet
            1, &pipelineInfo,
            nullptr,
            &m_Pipeline));

        // Shader modules are no longer needed after pipeline creation.
        vkDestroyShaderModule(m_Device->GetDevice(), vertModule, nullptr);
        vkDestroyShaderModule(m_Device->GetDevice(), fragModule, nullptr);

        if (!success)
            VCKLog::Error("Pipeline", "vkCreateGraphicsPipelines failed");

        return success;
    }

    bool VulkanPipeline::Reinitialize(
        VulkanDevice&          device,
        VulkanSwapchain&       swapchain,
        const ShaderInfo&      shaders,
        const VertexInputInfo& vertexInput,
        const Config&          pipelineConfig)
    {
        Shutdown();
        return Initialize(device, swapchain, shaders, vertexInput, pipelineConfig);
    }

} // namespace VCK