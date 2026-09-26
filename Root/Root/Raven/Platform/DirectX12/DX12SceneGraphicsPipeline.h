#pragma once

#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace Raven
{

// DX12 Scene PSO。Depth Attachmentは後続実装で接続します。
// b0のclip-space行列とb1のTintをRoot Constantsで渡します。
class DX12SceneGraphicsPipeline final : public RHIGraphicsPipeline
{
public:
    bool Init(ID3D12Device* device,
        const RHIGraphicsPipelineSpecification& specification)
    {
        if (device == nullptr ||
            specification.IsValidForBackend(RHIBackend::DirectX12) == false ||
            specification.Topology != PrimitiveTopology::Triangles ||
            specification.DepthFormat != RHIDepthFormat::D32Float ||
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
        // b0: clip-space行列(16 DWORD)、b1: Material Tint(4 DWORD)。
        // t0: RGBA8 Texture SRV。Samplerはs0のStatic Samplerです。
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 1;
        textureRange.BaseShaderRegister = 0;
        textureRange.RegisterSpace = 0;
        textureRange.OffsetInDescriptorsFromTableStart = 0;
        D3D12_ROOT_PARAMETER parameters[3]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.RegisterSpace = 0;
        parameters[0].Constants.Num32BitValues = 16;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants.ShaderRegister = 1;
        parameters[1].Constants.RegisterSpace = 0;
        parameters[1].Constants.Num32BitValues = 4;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[2].DescriptorTable.NumDescriptorRanges = 1;
        parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootDescription.NumParameters = 3;
        rootDescription.pParameters = parameters;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootDescription.NumStaticSamplers = 1;
        rootDescription.pStaticSamplers = &sampler;
        rootDescription.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT serializeResult = D3D12SerializeRootSignature(&rootDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            serialized.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(serializeResult))
        {
            std::cerr << "[DX12 Scene] Root Signature serialization failed: HRESULT 0x"
                << std::hex << static_cast<unsigned long>(serializeResult) << std::dec << "\n";
            if (errors != nullptr)
            {
                std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << "\n";
            }
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
        const HRESULT rootResult = device->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(rootSignature.GetAddressOf()));
        if (FAILED(rootResult))
        {
            std::cerr << "[DX12 Scene] CreateRootSignature failed: HRESULT 0x"
                << std::hex << static_cast<unsigned long>(rootResult) << std::dec << "\n";
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
        // PSOのTopology TypeとDraw時のIA topologyを一致させます。
        // Debug Line PipelineをTRIANGLEのまま生成すると、LINELISTを設定しても
        // D3D12のPipeline契約に反するためLinesを明示的に分岐します。
        description.PrimitiveTopologyType =
            specification.Topology == PrimitiveTopology::Lines ?
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE :
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.SampleMask = UINT_MAX;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = colorFormat;
        description.SampleDesc.Count = 1;
        description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        description.DepthStencilState.DepthEnable =
            specification.DepthTest == true ? TRUE : FALSE;
        description.DepthStencilState.DepthWriteMask =
            specification.DepthWrite == true ? D3D12_DEPTH_WRITE_MASK_ALL :
            D3D12_DEPTH_WRITE_MASK_ZERO;
        description.DepthStencilState.DepthFunc =
            specification.DepthCompare == DepthCompareOperator::Less ? D3D12_COMPARISON_FUNC_LESS :
            specification.DepthCompare == DepthCompareOperator::LessEqual ? D3D12_COMPARISON_FUNC_LESS_EQUAL :
            specification.DepthCompare == DepthCompareOperator::Greater ? D3D12_COMPARISON_FUNC_GREATER :
            specification.DepthCompare == DepthCompareOperator::GreaterEqual ? D3D12_COMPARISON_FUNC_GREATER_EQUAL :
            specification.DepthCompare == DepthCompareOperator::Equal ? D3D12_COMPARISON_FUNC_EQUAL :
            specification.DepthCompare == DepthCompareOperator::Always ? D3D12_COMPARISON_FUNC_ALWAYS :
            specification.DepthCompare == DepthCompareOperator::Never ? D3D12_COMPARISON_FUNC_NEVER :
            D3D12_COMPARISON_FUNC_LESS;
        description.DepthStencilState.StencilEnable = FALSE;

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
        const HRESULT pipelineResult = device->CreateGraphicsPipelineState(
            &description, IID_PPV_ARGS(pipeline.GetAddressOf()));
        if (FAILED(pipelineResult))
        {
            std::cerr << "[DX12 Scene] CreateGraphicsPipelineState failed: HRESULT 0x"
                << std::hex << static_cast<unsigned long>(pipelineResult) << std::dec << "\n";
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
