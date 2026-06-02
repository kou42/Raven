#pragma once

#include "../../Renderer/RendererAPI.h"

namespace Raven
{

class OpenGLRendererAPI : public RendererAPI
{

public:

    virtual void Init() override;
    virtual void SetClearColor(float r, float g, float b, float a) override;
    virtual void Clear() override;
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray) override;

    virtual void BindShader(const std::shared_ptr<Shader>& shader) override;
    virtual void BindTexture(const std::string& name, const std::shared_ptr<Texture>& texture, uint32_t slot) override;
    virtual void UploadUniform(const std::string& name, const UniformValue& value) override;

private:
    std::shared_ptr<Shader> m_CurrentShader;

};


}