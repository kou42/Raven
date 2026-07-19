#pragma once

#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"

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
    static void Draw(const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Material>& material, const math::Mat4& transform);

};

}