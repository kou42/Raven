#pragma once

//#include "Raven/Renderer/Layer/Layer.h"
//#include "Raven/Renderer/Shader/Shader.h"
//#include "Raven/Renderer/Buffer/VertexArray.h"
//#include "Raven/Core/Base.h"
//#include "Raven/Renderer/Texture/Texture.h"

#include "Layer.h"
#include "../Shader/Shader.h"
#include "../Buffer/VertexArray.h"
#include "../../Core/Base.h"
#include "../Texture/Texture.h"

namespace Raven
{

class SandboxLayer : public Layer
{

public:

    SandboxLayer();

    virtual void OnAttach() override;

    virtual void OnUpdate() override;

private:
    Ref<Shader> m_Shader;
    Ref<VertexArray> m_VertexArray;
    Ref<Texture>     m_Texture;
};

}