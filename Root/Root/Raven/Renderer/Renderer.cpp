#include "Renderer.h"

#include "RenderCommand.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Physics/Debug/PhysicsDebugRenderer.h"

#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"

namespace Raven
{

// 呼び出し元はApplication::Application()
void Renderer::Init()
{
    RenderCommand::SetAPI(std::make_unique<OpenGLRendererAPI>());
    RenderCommand::Init();
}

void Renderer::BeginScene()
{
    // 将来的には Camera 情報をここで受け取る
}

void Renderer::EndScene()
{
    // ========================================================================
    // Physics Debug Pass
    // ========================================================================
    // 通常のScene描画が完了した後に、Broad Phaseのデバッグ線を重ねます。
    // PhysicsDebugRenderer側のPipelineはDepthWrite=falseなので、デバッグ線が
    // 後続の深度バッファを汚染することはありません。
    ph::PhysicsDebugRenderer::RenderRegistered();
}

void Renderer::Shutdown()
{
}

RendererAPI& Renderer::GetAPI()
{
    return RenderCommand::GetAPI();
}

void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray)
{
    shader->Bind();
    vertexArray->Bind();
    RenderCommand::DrawIndexed(vertexArray);
}

void Renderer::DrawIndexed(const Ref<VertexArray>& vertexArray)
{
    RenderCommand::DrawIndexed(vertexArray);
}

void Renderer::Draw(const Ref<Mesh>& mesh, const Ref<Material>& material, const math::Mat4& transform)
{
    if (!mesh || !material)
    {
        return;
    }

    material->SetUniform("u_Model", transform);
    material->Bind(RenderCommand::GetAPI());
    mesh->Draw();
}

}
