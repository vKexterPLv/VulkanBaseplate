#pragma once

#include "VulkanHelpers.h"
#include <vector>
#include <cstdint>

namespace VCK {

    class VulkanDevice;
    class VulkanSwapchain;

    // ─────────────────────────────────────────────────────────────────────────────
    //  VulkanPipeline
    //
    //  Owns a VkRenderPass, VkPipelineLayout, and VkPipeline for a single
    //  graphics pass (colour output, no depth attachment by default).
    //
    //  Caller supplies:
    //    • ShaderInfo      - pre-compiled SPIR-V words for vertex + fragment stages
    //    • VertexInputInfo - binding + attribute descriptions for the vertex layout
    //
    //  Fixed pipeline state (sensible defaults, tweak as needed):
    //    • Topology    : TRIANGLE_LIST
    //    • Polygon     : FILL, back-face cull, COUNTER_CLOCKWISE front face
    //    • Blend       : alpha-blend (src_alpha / one_minus_src_alpha)
    //    • Depth       : disabled (add a DepthInfo struct when needed)
    //    • Viewport/Scissor : dynamic - set at draw time via vkCmdSetViewport/Scissor
    //
    //  Render pass:
    //    Single colour attachment matching swapchainFormat.
    //    loadOp  = CLEAR, storeOp = STORE
    //    initial = UNDEFINED, final = PRESENT_SRC_KHR
    //
    //  The render pass is exposed so VulkanFramebuffer (future) can use it.
    // ─────────────────────────────────────────────────────────────────────────────
    class VulkanPipeline
    {
    public:
        // ── Input descriptors ─────────────────────────────────────────────────────
        struct ShaderInfo
        {
            std::vector<uint32_t> VertexSpirv;
            std::vector<uint32_t> FragmentSpirv;
        };

        struct VertexInputInfo
        {
            std::vector<VkVertexInputBindingDescription>   Bindings;
            std::vector<VkVertexInputAttributeDescription> Attributes;
        };

        // -- Optional pipeline configuration --------------------------------------
        //
        // Per-pipeline knobs that used to be baked in.  All fields have sensible
        // defaults that match the pre-Config behaviour, so existing call sites
        // continue to work unchanged.  Pass a Config to any Initialize overload
        // to override:
        //
        //   VulkanPipeline::Config pc;
        //   pc.cullMode             = VK_CULL_MODE_NONE;
        //   pc.blendEnable          = false;
        //   pc.descriptorSetLayouts = { myDescLayout };
        //   pc.pushConstantRanges   = { { VK_SHADER_STAGE_VERTEX_BIT, 0, 64 } };
        //   pipeline.Initialize(device, swapchain, shaders, vi, pc);
        //
        // descriptorSetLayouts and pushConstantRanges are borrowed - the caller
        // keeps ownership, VulkanPipeline just hands the handles to
        // vkCreatePipelineLayout.  They must outlive Initialize() but do not
        // need to outlive the pipeline object.
        struct Config
        {
            // Rasterizer
            VkPrimitiveTopology   topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPolygonMode         polygonMode    = VK_POLYGON_MODE_FILL;
            VkCullModeFlags       cullMode       = VK_CULL_MODE_BACK_BIT;
            VkFrontFace           frontFace      = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            float                 lineWidth      = 1.0f;

            // Colour blend (single attachment matching the swapchain).  The
            // default matches the pre-Config behaviour: standard alpha blend.
            bool                  blendEnable         = true;
            VkBlendFactor         srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            VkBlendFactor         dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            VkBlendOp             colorBlendOp        = VK_BLEND_OP_ADD;
            VkBlendFactor         srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            VkBlendFactor         dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            VkBlendOp             alphaBlendOp        = VK_BLEND_OP_ADD;
            VkColorComponentFlags colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            // Pipeline layout (borrowed handles)
            std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
            std::vector<VkPushConstantRange>   pushConstantRanges;

            // Alpha-to-coverage (A2C).  Complements MSAA for alpha-tested
            // geometry (foliage, fences, decals).  Ignored when the pipeline
            // samples == 1x - Vulkan spec is a no-op there, but we guard it
            // anyway so cfg.pipeline.alphaToCoverage = true is always safe.
            bool                               alphaToCoverage      = false;

            // Sample-rate shading - evaluate the fragment shader once per
            // sample instead of once per pixel.  Improves interior AA at the
            // cost of shading rate.  Ignored when samples == 1x.  minRate is
            // passed through to VkPipelineMultisampleStateCreateInfo.minSampleShading
            // (1.0 = full rate, 0.25 = 1 in 4 samples, etc.).
            bool                               sampleRateShading    = false;
            float                              minSampleShadingRate = 0.2f;
        };


        // ─────────────────────────────────────────────────────────────────────────
        VulkanPipeline() = default;
        ~VulkanPipeline() = default;

        VulkanPipeline(const VulkanPipeline&) = delete;
        VulkanPipeline& operator=(const VulkanPipeline&) = delete;

        // Preferred overload - pulls the colour format AND MSAA sample
        // count from the swapchain (cfg.swapchain.msaaSamples).  Use this when
        // you want MSAA to follow the swapchain config.
        bool Initialize(VulkanDevice&          device,
                        VulkanSwapchain&       swapchain,
                        const ShaderInfo&      shaders,
                        const VertexInputInfo& vertexInput);

        bool Initialize(VulkanDevice&          device,
                        VulkanSwapchain&       swapchain,
                        const ShaderInfo&      shaders,
                        const VertexInputInfo& vertexInput,
                        const Config&          pipelineConfig);

        // Explicit-format / explicit-samples overload - MSAA = 1x by default.
        bool Initialize(VulkanDevice& device,
            VkFormat               swapchainFormat,
            const ShaderInfo& shaders,
            const VertexInputInfo& vertexInput,
            VkSampleCountFlagBits  samples = VK_SAMPLE_COUNT_1_BIT);

        bool Initialize(VulkanDevice&          device,
            VkFormat               swapchainFormat,
            const ShaderInfo&      shaders,
            const VertexInputInfo& vertexInput,
            VkSampleCountFlagBits  samples,
            const Config&          pipelineConfig);

        void Shutdown();

        // ── Accessors ────────────────────────────────────────────────────────────
        VkPipeline       GetPipeline()       const { return m_Pipeline; }
        VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
        VkRenderPass     GetRenderPass()     const { return m_RenderPass; }

    private:
        bool CreateRenderPass(VkFormat swapchainFormat);
        bool CreatePipelineLayout();
        bool CreateGraphicsPipeline(const ShaderInfo& shaders,
            const VertexInputInfo& vertexInput);

        VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);

        // ── State ────────────────────────────────────────────────────────────────
        VulkanDevice* m_Device = nullptr;
        VkRenderPass     m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline       m_Pipeline = VK_NULL_HANDLE;
        VkSampleCountFlagBits m_Samples = VK_SAMPLE_COUNT_1_BIT;

        // Snapshot of the caller's per-pipeline config.  Defaults match the
        // pre-Config behaviour, so zero-arg Initialize() is unchanged.
        Config m_PipelineCfg;
    };

} // namespace VCK