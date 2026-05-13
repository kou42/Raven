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
    virtual void DrawIndexed(/* VertexArray‚È‚Ç */) override;

};


}