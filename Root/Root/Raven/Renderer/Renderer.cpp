#include "Renderer.h"

#include "RenderCommand.h"

namespace Raven
{

// 呼び出し元はApplication::Application()
void Renderer::Init()
{
    // 将来的にはここで深度テスト、ブレンド、カリングなどを初期化する
    // 例:
    // RenderCommand::EnableDepthTest();
    // RenderCommand::EnableBlend();
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

void Renderer::Draw(
    const std::shared_ptr<Mesh>& mesh,
    const std::shared_ptr<Material>& material,
    const math::Mat4& transform
)
{
    if (!mesh || !material)
        return;

    // material->Set("u_Model", transform);
    // material->Bind(GetAPI());

    mesh->Draw();
}

}