#pragma once

#include "Raven/Renderer/RendererAPI.h"

namespace Raven
{

class OpenGLRendererAPI : public RendererAPI
{

public:

    virtual void Init() override;
    virtual void SetViewport(uint32_t x,uint32_t y, uint32_t width, uint32_t height) override;
    virtual void SetClearColor(float r, float g, float b, float a) override;
    virtual void Clear() override;
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;

    virtual void BindShader(const Ref<Shader>& shader) override;
    virtual void BindPipeline(const Ref<Pipeline>& pipeline) override;
    virtual void BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot) override;
    virtual void UploadUniform(const std::string& name, const UniformValue& value) override;

private:
    Ref<Shader> m_CurrentShader;
    Ref<VertexArray> m_CurrentVertexArray;

    Ref<Pipeline> m_CurrentPipeline;
};


}