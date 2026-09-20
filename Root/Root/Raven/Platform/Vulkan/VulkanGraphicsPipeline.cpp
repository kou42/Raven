#include "VulkanGraphicsPipeline.h"

#include <cstring>
#include <vector>

namespace Raven
{
namespace
{
VkFormat ToVertexFormat(ShaderDataType type)
{
    switch (type)
    {
    case ShaderDataType::Float: return VK_FORMAT_R32_SFLOAT;
    case ShaderDataType::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case ShaderDataType::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case ShaderDataType::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case ShaderDataType::Int: return VK_FORMAT_R32_SINT;
    case ShaderDataType::Int2: return VK_FORMAT_R32G32_SINT;
    case ShaderDataType::Int3: return VK_FORMAT_R32G32B32_SINT;
    case ShaderDataType::Int4: return VK_FORMAT_R32G32B32A32_SINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

VkFormat ToColorFormat(RHIColorFormat format)
{
    switch (format)
    {
    case RHIColorFormat::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case RHIColorFormat::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case RHIColorFormat::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case RHIColorFormat::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    default: return VK_FORMAT_UNDEFINED;
    }
}

VkPrimitiveTopology ToTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::Triangles: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::Lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::Points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    default: return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    }
}

VkCompareOp ToDepthCompare(DepthCompareOperator compare)
{
    switch (compare)
    {
    case DepthCompareOperator::Never: return VK_COMPARE_OP_NEVER;
    case DepthCompareOperator::Less: return VK_COMPARE_OP_LESS;
    case DepthCompareOperator::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case DepthCompareOperator::Equal: return VK_COMPARE_OP_EQUAL;
    case DepthCompareOperator::Greater: return VK_COMPARE_OP_GREATER;
    case DepthCompareOperator::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case DepthCompareOperator::Always: return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_LESS;
    }
}

bool CreateShaderModule(VkDevice device, const RHIShaderBinary& shader,
    VkShaderModule& module)
{
    // SPIR-Vは32bit word列。uint8_t vectorを直接uint32_t*へ
    // reinterpret_castせず、アラインメントを満たす領域へコピーします。
    if (shader.Code.empty() == true || shader.Code.size() % sizeof(uint32_t) != 0)
    {
        return false;
    }
    std::vector<uint32_t> words(shader.Code.size() / sizeof(uint32_t));
    std::memcpy(words.data(), shader.Code.data(), shader.Code.size());
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = shader.Code.size();
    info.pCode = words.data();
    return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS;
}
} // namespace

VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
{
    Shutdown();
}

bool VulkanGraphicsPipeline::Init(VkDevice device, VkRenderPass renderPass,
    VkFormat colorFormat, const RHIGraphicsPipelineSpecification& specification)
{
    Shutdown();
    // 現行VulkanSceneRenderTargetはColor Attachmentのみ、SampleCount=1です。
    // Depth/StencilやDescriptorを必要とするShaderは後続の実装で対応します。
    if (device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE ||
        specification.IsValidForBackend(RHIBackend::Vulkan) == false ||
        specification.DepthFormat != RHIDepthFormat::None ||
        specification.DepthTest == true || specification.DepthWrite == true ||
        specification.SampleCount != 1 ||
        ToColorFormat(specification.ColorFormat) != colorFormat ||
        ToTopology(specification.Topology) == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM)
    {
        return false;
    }

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const RHIVertexBinding& binding : specification.VertexBindings)
    {
        VkVertexInputBindingDescription native{};
        native.binding = binding.Binding;
        native.stride = binding.Stride;
        native.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindings.push_back(native);
    }
    for (const RHIVertexAttribute& attribute : specification.VertexAttributes)
    {
        const VkFormat format = ToVertexFormat(attribute.Type);
        if (format == VK_FORMAT_UNDEFINED)
        {
            return false;
        }
        VkVertexInputAttributeDescription native{};
        native.location = attribute.Location;
        native.binding = attribute.Binding;
        native.format = format;
        native.offset = attribute.Offset;
        attributes.push_back(native);
    }

    m_Device = device;
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (CreateShaderModule(device, specification.VertexShader, vertexModule) == false ||
        CreateShaderModule(device, specification.FragmentShader, fragmentModule) == false)
    {
        if (vertexModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, vertexModule, nullptr);
        }
        if (fragmentModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, fragmentModule, nullptr);
        }
        Shutdown();
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = specification.VertexShader.EntryPoint.c_str();
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = specification.FragmentShader.EntryPoint.c_str();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = ToTopology(specification.Topology);

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = specification.Cull == CullMode::None ?
        VK_CULL_MODE_NONE : (specification.Cull == CullMode::Front ?
            VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
    rasterizer.frontFace = specification.FrontFaceMode == FrontFace::Clockwise ?
        VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthCompareOp = ToDepthCompare(specification.DepthCompare);

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = specification.Blend == true ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    // VulkanSceneContext::SetViewportがFrameごとに記録するDynamic Stateと一致させます。
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkResult layoutResult = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_Layout);
    if (layoutResult == VK_SUCCESS)
    {
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = m_Layout;
        info.renderPass = renderPass;
        info.subpass = 0;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_Pipeline);
    }
    vkDestroyShaderModule(device, fragmentModule, nullptr);
    vkDestroyShaderModule(device, vertexModule, nullptr);
    if (m_Pipeline == VK_NULL_HANDLE)
    {
        Shutdown();
        return false;
    }
    m_Specification = specification;
    return true;
}

void VulkanGraphicsPipeline::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        // GPUが参照中のPipelineを破棄しないよう、呼び出し元がFence完了を保証します。
        if (m_Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
        }
        if (m_Layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
        }
    }
    m_Pipeline = VK_NULL_HANDLE;
    m_Layout = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_Specification = {};
}
} // namespace Raven
