#pragma once

#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Core/Base.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{

class SandboxLayer : public Layer
{

public:

    SandboxLayer();

    virtual void OnAttach() override;

    virtual void OnUpdate(float dt) override;

private:
    ShaderLibrary m_ShaderLibrary;
    Ref<Shader> m_Shader;
    Ref<VertexArray> m_VertexArray;

    TextureLibrary m_TextureLibrary;
    Ref<Texture>     m_Texture;
};

}