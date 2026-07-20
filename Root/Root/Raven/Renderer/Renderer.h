#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{

namespace math
{
	struct Mat4;
}

class Material;
class Mesh;
class RendererAPI;
class Shader;
class VertexArray;

class Renderer
{
public:
    static void Init();
    static void Shutdown();

    static void BeginScene();
    static void EndScene();

    static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray);
    static void DrawIndexed(const Ref<VertexArray>& vertexArray);
    static void Draw(const Ref<Mesh>& mesh, const Ref<Material>& material, const math::Mat4& transform);

    static RendererAPI& GetAPI();
};

}