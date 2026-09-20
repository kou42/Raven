#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Raven/Renderer/Buffer/BufferLayout.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

// Shaderのバイナリ形式はBackendごとに異なるため、同一バイト列を
// VulkanとDirectX12の両方へ渡せるものとして扱わない。
enum class RHIShaderBinaryFormat
{
    None = 0,
    SPIRV,
    DXIL
};

struct RHIShaderBinary
{
    RHIShaderBinaryFormat Format = RHIShaderBinaryFormat::None;
    std::vector<uint8_t> Code;
    std::string EntryPoint = "main";
};

// VertexBufferのBufferLayoutとは独立した、Pipeline作成時に固定する入力宣言。
// LocationはShaderの入力位置、BindingはVertexBufferのスロットを表す。
struct RHIVertexAttribute
{
    uint32_t Location = 0;
    uint32_t Binding = 0;
    ShaderDataType Type = ShaderDataType::None;
    uint32_t Offset = 0;
};

struct RHIVertexBinding
{
    uint32_t Binding = 0;
    uint32_t Stride = 0;
};

enum class RHIColorFormat
{
    None = 0,
    RGBA8Unorm,
    BGRA8Unorm
};

enum class RHIDepthFormat
{
    None = 0,
    D24UnormS8Uint,
    D32Float
};

// Sceneの描画先とShader入力を含むGraphics Pipelineの共通作成情報。
// ColorFormatは実際のScene RenderTargetと一致させる必要がある。
// VulkanのRenderPass互換性やDX12のRoot Signature等のnative詳細は
// Backend実装側で解決し、この構造体へnative handleを持ち込まない。
struct RHIGraphicsPipelineSpecification
{
    RHIShaderBinary VertexShader;
    RHIShaderBinary FragmentShader;

    std::vector<RHIVertexBinding> VertexBindings;
    std::vector<RHIVertexAttribute> VertexAttributes;

    PrimitiveTopology Topology = PrimitiveTopology::Triangles;
    CullMode Cull = CullMode::Back;
    FrontFace FrontFaceMode = FrontFace::CounterClockwise;

    DepthCompareOperator DepthCompare = DepthCompareOperator::Less;
    bool DepthTest = false;
    bool DepthWrite = false;
    bool Blend = false;

    RHIColorFormat ColorFormat = RHIColorFormat::None;
    RHIDepthFormat DepthFormat = RHIDepthFormat::None;
    uint32_t SampleCount = 1;

    std::string DebugName = "Unnamed RHI Graphics Pipeline";

    // native API呼び出し前に共通の入力不備を検出します。
    // Format/RenderPass/Root SignatureなどBackend固有の互換性は別途確認が必要です。
    bool IsValidForBackend(RHIBackend backend) const
    {
        RHIShaderBinaryFormat requiredFormat = RHIShaderBinaryFormat::None;
        switch (backend)
        {
        case RHIBackend::Vulkan:
            requiredFormat = RHIShaderBinaryFormat::SPIRV;
            break;
        case RHIBackend::DirectX12:
            requiredFormat = RHIShaderBinaryFormat::DXIL;
            break;
        case RHIBackend::OpenGL:
        case RHIBackend::DirectX11:
        case RHIBackend::None:
        default:
            return false;
        }

        if (VertexShader.Format != requiredFormat ||
            FragmentShader.Format != requiredFormat ||
            VertexShader.Code.empty() == true ||
            FragmentShader.Code.empty() == true ||
            VertexShader.EntryPoint.empty() == true ||
            FragmentShader.EntryPoint.empty() == true ||
            ColorFormat == RHIColorFormat::None ||
            SampleCount == 0 ||
            (DepthTest == true && DepthFormat == RHIDepthFormat::None) ||
            (DepthWrite == true && DepthFormat == RHIDepthFormat::None))
        {
            return false;
        }

        for (std::size_t index = 0; index < VertexBindings.size(); ++index)
        {
            if (VertexBindings[index].Stride == 0)
            {
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (VertexBindings[previous].Binding == VertexBindings[index].Binding)
                {
                    return false;
                }
            }
        }

        for (std::size_t index = 0; index < VertexAttributes.size(); ++index)
        {
            const RHIVertexAttribute& attribute = VertexAttributes[index];
            const uint32_t size = ShaderDataTypeSize(attribute.Type);
            bool bindingFound = false;
            for (const RHIVertexBinding& binding : VertexBindings)
            {
                if (binding.Binding == attribute.Binding)
                {
                    bindingFound = true;
                    if (size == 0 || attribute.Offset > binding.Stride ||
                        size > binding.Stride - attribute.Offset)
                    {
                        return false;
                    }
                    break;
                }
            }
            if (bindingFound == false)
            {
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (VertexAttributes[previous].Location == attribute.Location)
                {
                    return false;
                }
            }
        }
        return true;
    }
};

} // namespace Raven
