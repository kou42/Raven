#pragma once

#include <vulkan/vulkan.h>

#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"

namespace Raven
{

// Scene Color RenderPass用の最小Graphics Pipelineです。
// VkDeviceとVkRenderPassは借用し、ShutdownはDevice破棄より先に実行してください。
// ResizeでRenderPassが再生成された場合も、このPipelineを作り直します。
class VulkanGraphicsPipeline final : public RHIGraphicsPipeline
{
public:
    ~VulkanGraphicsPipeline() override;

    VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;

    VulkanGraphicsPipeline() = default;

    bool Init(VkDevice device, VkRenderPass renderPass, VkFormat colorFormat,
        const RHIGraphicsPipelineSpecification& specification);
    void Shutdown();

    const RHIGraphicsPipelineSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

    VkPipeline GetHandle() const { return m_Pipeline; }
    VkPipelineLayout GetLayout() const { return m_Layout; }
    bool IsValid() const { return m_Pipeline != VK_NULL_HANDLE; }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    RHIGraphicsPipelineSpecification m_Specification;
};

} // namespace Raven
