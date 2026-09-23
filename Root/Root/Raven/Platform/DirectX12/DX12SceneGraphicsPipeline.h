#pragma once

#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <vector>

namespace Raven
{

// 最初のDX12 Scene PSO。Texture/定数Buffer/Depth Attachmentは後続実装で接続します。
// 現段階ではResource Bindingを要求しないDXIL Shaderのみが描画可能です。
class DX12SceneGraphicsPipeline final : public RHIGraphicsPipeline
{
public:
    bool Init(ID3D12Device* device,
        const RHIGraphicsPipelineSpecification& specification)
    {
        if (device == nullptr ||
            specification.IsValidForBackend(RHIBackend::DirectX12) == false ||
            specification.Topology != PrimitiveTopology::Triangles ||
            specification.DepthTest == true || specification.DepthWrite == true ||
            specification.DepthFormat != RHIDepthFormat::None ||
            specification.SampleCount != 1 ||
            specification.VertexBindings.size() != 1 ||
            specification.VertexBindings[0].Binding != 0)
        {
            return false;
        }

        DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
        switch (specification.ColorFormat)
        {
        case RHIColorFormat::RGBA8Unorm:
            colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case RHIColorFormat::RGBA8Srgb:
            colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            break;
        case RHIColorFormat::BGRA8Unorm:
            colorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case RHIColorFormat::BGRA8Srgb:
            colorFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            break;
        default:
            return false;
        }

        std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
        elements.reserve(specification.VertexAttributes.size());
        // semantic文字列はPSO生成まで保持します。DXILの入力Semanticと一致させます。
        static constexpr std::array<const char*, 4> semantics =
            { "POSITION", "COLOR", "TEXCOORD", "NORMAL" };
        for (const RHIVertexAttribute& attribute : specification.VertexAttributes)
        {
            if (attribute.Binding != 0 || attribute.Location >= semantics.size())
            {
                return false;
            }
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            switch (attribute.Type)
            {
            case ShaderDataType::Float:
                format = DXGI_FORMAT_R32_FLOAT;
                break;
            case ShaderDataType::Float2:
                format = DXGI_FORMAT_R32G32_FLOAT;
                break;
            case ShaderDataType::Float3:
                format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case ShaderDataType::Float4:
                format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            default:
                return false;
            }
            D3D12_INPUT_ELEMENT_DESC element{};
            element.SemanticName = semantics[attribute.Location];
            element.SemanticIndex = 0;
            element.Format = format;
            element.InputSlot = 0;
            element.AlignedByteOffset = attribute.Offset;
            element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            elements.push_back(element);
        }

        // ShaderにDescriptor/Root Constantが必要な場合、空Root Signatureでは
        // PSO生成に失敗します。未対応Bindingを暗黙に成功扱いしません。
        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        if (FAILED(D3D12SerializeRootSignature(&rootDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            serialized.GetAddressOf(), errors.GetAddressOf())))
        {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
        if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(rootSignature.GetAddressOf()))))
        {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature.Get();
        description.VS = { specification.VertexShader.Code.data(),
            specification.VertexShader.Code.size() };
        description.PS = { specification.FragmentShader.Code.data(),
            specification.FragmentShader.Code.size() };
        description.InputLayout = { elements.data(),
            static_cast<UINT>(elements.size()) };
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.SampleMask = UINT_MAX;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = colorFormat;
        description.SampleDesc.Count = 1;
        description.DSVFormat = DXGI_FORMAT_UNKNOWN;

        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode =
            specification.Cull == CullMode::None ? D3D12_CULL_MODE_NONE :
            specification.Cull == CullMode::Front ? D3D12_CULL_MODE_FRONT :
            D3D12_CULL_MODE_BACK;
        description.RasterizerState.FrontCounterClockwise =
            specification.FrontFaceMode == FrontFace::CounterClockwise;
        description.RasterizerState.DepthClipEnable = TRUE;

        D3D12_RENDER_TARGET_BLEND_DESC& blend =
            description.BlendState.RenderTarget[0];
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        blend.BlendEnable = specification.Blend == true ? TRUE : FALSE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;

        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
        if (FAILED(device->CreateGraphicsPipelineState(
            &description, IID_PPV_ARGS(pipeline.GetAddressOf()))))
        {
            return false;
        }
        m_RootSignature = std::move(rootSignature);
        m_Pipeline = std::move(pipeline);
        m_Specification = specification;
        m_OwnerDevice = device;
        return true;
    }

    const RHIGraphicsPipelineSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

    ID3D12PipelineState* GetNativePipeline() const { return m_Pipeline.Get(); }
    ID3D12RootSignature* GetNativeRootSignature() const
    {
        return m_RootSignature.Get();
    }
    ID3D12Device* GetOwnerDevice() const { return m_OwnerDevice; }

private:
    RHIGraphicsPipelineSpecification m_Specification{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pipeline;
    ID3D12Device* m_OwnerDevice = nullptr;
};

} // namespace Raven
