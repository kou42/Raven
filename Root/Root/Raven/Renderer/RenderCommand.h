#pragma once

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Core/Base.h"

namespace Raven
{

class RenderCommand
{
public:

    static void Init();

    static void SetClearColor(float r, float g, float b, float a);

    static void Clear();

    static void DrawIndexed(const Ref<VertexArray>& vertexArray);

private:
    static Scope<RendererAPI> s_RendererAPI;
};

}