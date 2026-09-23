#pragma once

#include "Raven/Renderer/RHI/RHICommandList.h"

namespace Raven
{

class OpenGLRHICommandList final : public RHICommandList
{
public:
    ~OpenGLRHICommandList() override = default;

    void Init() override;
    void BindDefaultRenderTarget() override;
    void BindRenderTarget(const Framebuffer& framebuffer) override;
    RHIRenderTargetState CaptureRenderTargetState() const override;
    void RestoreRenderTargetState(const RHIRenderTargetState& state) override;
    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    RHIViewport GetViewport() const override;
    void SetScissor(bool enabled, int32_t x, int32_t y, uint32_t width, uint32_t height) override;
    RHIScissor GetScissor() const override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear() override;

    void BindPipeline(const Ref<Pipeline>& pipeline) override;
    void RestorePipelineBinding(const Ref<Pipeline>& pipeline) override;
    void BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot) override;
    void UploadUniform(const std::string& name, const UniformValue& value) override;
    void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0, uint32_t firstIndex = 0) override;

private:
    // DrawIndexedのPrimitiveTopologyとTexture / UniformのShader解決に使用します。
    // OpenGLはImmediate APIですが、Explicit APIのCommandListと同様に現在の描画stateを保持します。
    Ref<Pipeline> m_CurrentPipeline;
};

} // namespace Raven
