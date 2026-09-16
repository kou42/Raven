#pragma once

#include "Raven/Renderer/RHI/RHICommandList.h"

namespace Raven
{

class OpenGLRHICommandList final : public RHICommandList
{
public:
    ~OpenGLRHICommandList() override = default;

    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear() override;

    void BindPipeline(const Ref<Pipeline>& pipeline) override;
    void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;

private:
    // DrawIndexed時のPrimitiveTopology解決に使用します。
    // OpenGLはImmediate APIですが、Explicit APIのCommandListと同様に現在の描画stateを保持します。
    Ref<Pipeline> m_CurrentPipeline;
};

} // namespace Raven
