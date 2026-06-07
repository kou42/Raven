#include "Renderer.h"

#include "RenderCommand.h"

namespace Raven
{
void Renderer::Init()
{
    // 将来的にはここで深度テスト、ブレンド、カリングなどを初期化する
    // 例:
    // RenderCommand::EnableDepthTest();
    // RenderCommand::EnableBlend();
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

}