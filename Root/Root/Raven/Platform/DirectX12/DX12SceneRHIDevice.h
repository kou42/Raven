#pragma once

#include "DX12SceneContext.h"
#include "DX12SceneGraphicsPipeline.h"
#include "DX12SceneRHIBuffer.h"
#include "DX12SceneRHITexture.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

// DX12SceneContextが所有するDeviceを借用し、Entity MeshのGPU Bufferを生成します。
// Textureは初期RGBA8転送のみ対応し、SRV Bindingは後続実装で接続します。
class DX12SceneRHIDevice final : public RHIDevice
{
public:
    explicit DX12SceneRHIDevice(DX12SceneContext& context)
        : m_Context(context)
    {
    }

    RHIBackend GetBackend() const override
    {
        return RHIBackend::DirectX12;
    }

    // Vulkan側と同じScene入口で共有Meshを1回ずつ準備します。
    // DX12のDraw Commandが揃うまでは描画自体は呼び出し側で行いません。
    bool PrepareScene(Scene& scene)
    {
        if (m_Context.GetNativeDevice() == nullptr ||
            m_Context.GetActiveCommandList() != nullptr)
        {
            return false;
        }
        return scene.PrepareRHIMeshes(*this);
    }

    Ref<RHIBuffer> CreateBuffer(
        const RHIBufferSpecification& specification,
        const void* initialData = nullptr) override
    {
        // Frame記録中のResource生成を禁止し、Scene Contextと同じDeviceを使用します。
        if (m_Context.GetActiveCommandList() != nullptr ||
            m_Context.GetNativeDevice() == nullptr)
        {
            return nullptr;
        }

        auto buffer = CreateRef<DX12SceneRHIBuffer>();
        if (buffer->Init(
            m_Context.GetNativeDevice(), specification, initialData) == false)
        {
            return nullptr;
        }
        return buffer;
    }

    Ref<RHIGraphicsPipeline> CreateGraphicsPipeline(
        const RHIGraphicsPipelineSpecification& specification) override
    {
        if (m_Context.GetNativeDevice() == nullptr ||
            m_Context.GetActiveCommandList() != nullptr)
        {
            return nullptr;
        }
        auto pipeline = CreateRef<DX12SceneGraphicsPipeline>();
        if (pipeline->Init(m_Context.GetNativeDevice(), specification) == false)
        {
            return nullptr;
        }
        return pipeline;
    }

    bool GetGraphicsPipelineTarget(
        RHIGraphicsPipelineTarget& target) const override
    {
        target = {};
        if (m_Context.GetNativeDevice() == nullptr)
        {
            return false;
        }
        // DX12SwapChainのRTVはRGBA8 UNORM、Scene DepthはD32_FLOATです。
        target.ColorFormat = RHIColorFormat::RGBA8Unorm;
        target.DepthFormat = RHIDepthFormat::D32Float;
        target.SampleCount = 1;
        return true;
    }

    bool PrepareSceneTextures(
        const std::vector<Ref<RHITexture>>& textures,
        const Ref<RHIGraphicsPipeline>& pipeline) override
    {
        // DX12はTexture生成時にSRV Heapを作成済みです。
        // BeginFrame前に全Textureが同一DeviceのShader-visible SRVを持つことを検証します。
        // RHIDeviceの既定実装はfalseを返すため、これを省くと初回Drawが必ず失敗します。
        auto nativePipeline =
            std::dynamic_pointer_cast<DX12SceneGraphicsPipeline>(pipeline);
        if (m_Context.GetNativeDevice() == nullptr ||
            m_Context.GetActiveCommandList() != nullptr ||
            nativePipeline == nullptr ||
            nativePipeline->GetOwnerDevice() != m_Context.GetNativeDevice())
        {
            return false;
        }
        for (const Ref<RHITexture>& texture : textures)
        {
            auto nativeTexture =
                std::dynamic_pointer_cast<DX12SceneRHITexture>(texture);
            if (nativeTexture == nullptr ||
                nativeTexture->GetOwnerDevice() != m_Context.GetNativeDevice() ||
                nativeTexture->GetNativeTexture() == nullptr ||
                nativeTexture->GetSrvHeap() == nullptr)
            {
                return false;
            }
        }
        return true;
    }

    Ref<RHITexture> CreateTexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0) override
    {
        if (m_Context.GetNativeDevice() == nullptr ||
            m_Context.GetActiveCommandList() != nullptr)
        {
            return nullptr;
        }
        auto texture = CreateRef<DX12SceneRHITexture>();
        if (texture->Init(m_Context.GetNativeDevice(),
            specification, initialData, initialDataSize) == false)
        {
            return nullptr;
        }
        return texture;
    }

private:
    DX12SceneContext& m_Context;
};

} // namespace Raven
