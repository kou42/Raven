#pragma once

#include "DX12SceneContext.h"
#include "DX12SceneGraphicsPipeline.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

#include <memory>

namespace Raven
{

// Explicit DX12 Sceneの共通CommandList Adapter。
// ContextがFrame/CommandListを所有し、ここでは借用します。
class DX12SceneCommandList final : public RHISceneCommandList
{
public:
    explicit DX12SceneCommandList(DX12SceneContext& context)
        : m_Context(context)
    {
    }

    DX12SceneCommandList(const DX12SceneCommandList&) = delete;
    DX12SceneCommandList& operator=(const DX12SceneCommandList&) = delete;

    bool SetViewport(uint32_t x, uint32_t y,
        uint32_t width, uint32_t height) override
    {
        return m_Context.SetViewport(x, y, width, height);
    }

    bool ClearColor(const float color[4]) override
    {
        return m_Context.ClearColorAttachment(color);
    }

    bool BindPipeline(const Ref<RHIGraphicsPipeline>& pipeline) override
    {
        auto native = std::dynamic_pointer_cast<DX12SceneGraphicsPipeline>(pipeline);
        if (native == nullptr ||
            native->GetOwnerDevice() != m_Context.GetNativeDevice() ||
            native->GetNativePipeline() == nullptr ||
            native->GetNativeRootSignature() == nullptr ||
            native->GetSpecification().VertexBindings.size() != 1 ||
            native->GetSpecification().VertexBindings[0].Binding != 0)
        {
            return false;
        }

        if (m_Context.BindGraphicsPipeline(native->GetNativePipeline(),
            native->GetNativeRootSignature()) == false ||
            m_Context.RetainGraphicsPipeline(pipeline) == false)
        {
            return false;
        }
        // 同一FrameでPipelineを切り替えた際も次のDrawのstrideを更新します。
        m_Pipeline = native;
        return true;
    }

    bool BindMaterial(const RHIMaterialProperties& material) override
    {
        if (material.SurfaceType == MaterialSurfaceType::Masked ||
            material.Texture == nullptr || m_Pipeline == nullptr ||
            m_Context.IsGraphicsPipelineBound() == false ||
            m_Context.BindTexture(material.Texture) == false)
        {
            return false;
        }
        return m_Context.SetMaterialTint(material.Tint);
    }

    bool SetClipTransform(const std::array<float, 16>& transform) override
    {
        if (m_Pipeline == nullptr ||
            m_Context.IsGraphicsPipelineBound() == false)
        {
            return false;
        }
        return m_Context.SetClipTransform(transform);
    }

    bool DrawIndexed(const Ref<RHIBuffer>& vertexBuffer,
        const Ref<RHIBuffer>& indexBuffer, uint32_t indexCount = 0) override
    {
        if (m_Pipeline == nullptr ||
            m_Context.IsGraphicsPipelineBound() == false ||
            m_Pipeline->GetOwnerDevice() != m_Context.GetNativeDevice())
        {
            return false;
        }
        return m_Context.DrawIndexed(vertexBuffer, indexBuffer,
            m_Pipeline->GetSpecification().VertexBindings[0].Stride, indexCount);
    }

private:
    DX12SceneContext& m_Context;
    Ref<DX12SceneGraphicsPipeline> m_Pipeline;
};

} // namespace Raven
