#pragma once

#include "DX12SceneContext.h"
#include "DX12SceneGraphicsPipeline.h"
#include "DX12SceneRHIBuffer.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

// DX12SceneContextが所有するDeviceを借用し、Entity MeshのGPU Bufferを生成します。
// Texture/Pipelineは未接続なので、未対応操作を成功扱いしません。
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
        // DX12SwapChainの現行RTVはRGBA8 UNORM、Depthは未実装です。
        target.ColorFormat = RHIColorFormat::RGBA8Unorm;
        target.DepthFormat = RHIDepthFormat::None;
        target.SampleCount = 1;
        return true;
    }

    Ref<RHITexture> CreateTexture(
        const RHITextureSpecification& specification,
        const void* initialData = nullptr,
        std::size_t initialDataSize = 0) override
    {
        (void)specification;
        (void)initialData;
        (void)initialDataSize;
        return nullptr;
    }

private:
    DX12SceneContext& m_Context;
};

} // namespace Raven
