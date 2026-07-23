#include "Renderer.h"

#include "RenderCommand.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Material/Material.h"

#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"

namespace Raven
{

// 呼び出し元はApplication::Application()
void Renderer::Init()
{
    // 将来的にはここで深度テスト、ブレンド、カリングなどを初期化する
    // 例:
    // RenderCommand::EnableDepthTest();
    // RenderCommand::EnableBlend();
    RenderCommand::SetAPI(std::make_unique<OpenGLRendererAPI>());
    RenderCommand::Init();
}

void Renderer::BeginScene()
{
    // 将来的には Camera 情報をここで受け取る
    // 例:
    // Renderer::BeginScene(camera);
}

void Renderer::EndScene()
{
    // 今は何もしない
}

void Renderer::Shutdown()
{
    // 必要になった段階で
    // RenderCommand::Shutdown() を追加してもよい
}

RendererAPI& Renderer::GetAPI()
{
    return RenderCommand::GetAPI();
}

void Renderer::Submit( const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray)
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
    if (!mesh || !material) {
        return;
    }

#if 1
    material->SetUniform("u_Model", transform);

    material->Bind(RenderCommand::GetAPI());

    mesh->Draw();
#else
    material->Set("u_Model", transform);
    material->Bind(GetAPI());
    mesh->Draw();
#endif
    
}

}