#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Raven/Renderer/Buffer/BufferLayout.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"

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
};

} // namespace Raven
