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
    //    • ShaderInfo      — pre-compiled SPIR-V words for vertex + fragment stages
    //    • VertexInputInfo — binding + attribute descriptions for the vertex layout
    //
    //  Fixed pipeline state (sensible defaults, tweak as needed):
    //    • Topology    : TRIANGLE_LIST
    //    • Polygon     : FILL, back-face cull, COUNTER_CLOCKWISE front face
    //    • Blend       : alpha-blend (src_alpha / one_minus_src_alpha)
    //    • Depth       : disabled (add a DepthInfo struct when needed)
    //    • Viewport/Scissor : dynamic — set at draw time via vkCmdSetViewport/Scissor
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

        // ─────────────────────────────────────────────────────────────────────────
        VulkanPipeline() = default;
        ~VulkanPipeline() = default;

        VulkanPipeline(const VulkanPipeline&) = delete;
        VulkanPipeline& operator=(const VulkanPipeline&) = delete;

        // Preferred overload — pulls the colour format AND MSAA sample
        // count from the swapchain (cfg.swapchain.msaaSamples).  Use this when
        // you want MSAA to follow the swapchain config.
        bool Initialize(VulkanDevice&          device,
                        VulkanSwapchain&       swapchain,
                        const ShaderInfo&      shaders,
                        const VertexInputInfo& vertexInput);

        // Explicit-format / explicit-samples overload — MSAA = 1x by default.
        bool Initialize(VulkanDevice& device,
            VkFormat               swapchainFormat,
            const ShaderInfo& shaders,
            const VertexInputInfo& vertexInput,
            VkSampleCountFlagBits  samples = VK_SAMPLE_COUNT_1_BIT);

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
    };

} // namespace VCK