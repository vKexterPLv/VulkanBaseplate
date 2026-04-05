#include "VulkanPipeline.h"
#include "VulkanDevice.h"
#include <array>

namespace GTA_Sandbox {

    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanPipeline::Initialize(VulkanDevice& device,
        VkFormat               swapchainFormat,
        const ShaderInfo& shaders,
        const VertexInputInfo& vertexInput)
    {
        m_Device = &device;

        if (!CreateRenderPass(swapchainFormat))    return false;
        if (!CreatePipelineLayout())               return false;
        if (!CreateGraphicsPipeline(shaders, vertexInput)) return false;

        LogVk("VulkanPipeline initialized");
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
        LogVk("VulkanPipeline shut down");
    }


    // ─────────────────────────────────────────────────────────────────────────────
    //  Render pass
    //  Single colour attachment.  loadOp = CLEAR, storeOp = STORE.
    //  Subpass: colour only, layout ATTACHMENT_OPTIMAL during pass.
    //  Final layout: PRESENT_SRC_KHR — ready for vkQueuePresentKHR.
    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanPipeline::CreateRenderPass(VkFormat swapchainFormat)
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        // Dependency: wait for the previous present to finish writing before we
        // start writing colour again.
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        return VK_CHECK(vkCreateRenderPass(m_Device->GetDevice(), &renderPassInfo, nullptr, &m_RenderPass));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Pipeline layout
    //  No descriptor sets, no push constants yet — add them when needed.
    // ─────────────────────────────────────────────────────────────────────────────
    bool VulkanPipeline::CreatePipelineLayout()
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pSetLayouts = nullptr;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        return VK_CHECK(vkCreatePipelineLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &m_PipelineLayout));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    VkShaderModule VulkanPipeline::CreateShaderModule(const std::vector<uint32_t>& spirv)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (!VK_CHECK(vkCreateShaderModule(m_Device->GetDevice(), &createInfo, nullptr, &shaderModule)))
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
            if (vertModule) vkDestroyShaderModule(m_Device->GetDevice(), vertModule, nullptr);
            if (fragModule) vkDestroyShaderModule(m_Device->GetDevice(), fragModule, nullptr);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragModule;
        shaderStages[1].pName = "main";

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
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // ── Viewport + scissor (dynamic — set at draw time) ───────────────────────
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
        // pViewports / pScissors left null — filled by vkCmdSetViewport/Scissor

        // ── Rasterizer ────────────────────────────────────────────────────────────
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        // ── Multisampling (off) ───────────────────────────────────────────────────
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        // ── Depth / stencil (disabled — add when needed) ──────────────────────────
        // Pass nullptr for pDepthStencilState when no depth attachment exists.

        // ── Colour blend — standard alpha blend ──────────────────────────────────
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // ── Assemble ──────────────────────────────────────────────────────────────
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
        pipelineInfo.renderPass = m_RenderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        bool success = VK_CHECK(vkCreateGraphicsPipelines(
            m_Device->GetDevice(),
            VK_NULL_HANDLE,   // no pipeline cache yet
            1, &pipelineInfo,
            nullptr,
            &m_Pipeline));

        // Shader modules are no longer needed after pipeline creation.
        vkDestroyShaderModule(m_Device->GetDevice(), vertModule, nullptr);
        vkDestroyShaderModule(m_Device->GetDevice(), fragModule, nullptr);

        return success;
    }

} // namespace GTA_Sandbox