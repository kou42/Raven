#include "RenderCommand.h"
#include "../Platform/OpenGL/OpenGLRendererAPI.h"

namespace Raven
{

Scope<RendererAPI> RenderCommand::s_RendererAPI = CreateScope<OpenGLRendererAPI>();

void RenderCommand::Init()
{
    // 将来的には下記コードに差し替え予定
#if 0
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
    {
        s_RendererAPI = CreateScope<OpenGLRendererAPI>();
        break;
    }
    case RendererAPI::API::DirectX11:
    {
        s_RendererAPI = CreateScope<DX11RendererAPI>();
        break;
    }
    }
#endif
    s_RendererAPI->Init();
}

void RenderCommand::SetClearColor(float r, float g, float b, float a)
{
    s_RendererAPI->SetClearColor(r, g, b, a);
}

void RenderCommand::Clear()
{
    s_RendererAPI->Clear();
}

void RenderCommand::DrawIndexed(const Ref<VertexArray>& vertexArray)
{
    s_RendererAPI->DrawIndexed(vertexArray);
}

}