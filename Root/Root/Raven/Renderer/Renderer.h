#pragma once

#include "Shader/Shader.h"
#include "Buffer/VertexArray.h"

namespace Raven
{

class Renderer
{
public:
    static void Init();

    static void BeginScene();
    static void EndScene();

    static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray);
    static void DrawIndexed(const Ref<VertexArray>& vertexArray);

};

}