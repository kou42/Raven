#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"

namespace Raven
{

// ��`
#if 1
Scope<RendererAPI> RenderCommand::s_RendererAPI = nullptr;
#else
Scope<RendererAPI> RenderCommand::s_RendererAPI = CreateScope<OpenGLRendererAPI>();
#endif

// Renderer::Init()���Ăяo����
void RenderCommand::Init()
{
    if (s_RendererAPI) {
        return;
    }

    // �����I�ɂ͉��L�R�[�h�ɍ����ւ��\��
#if 1
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
    {
        s_RendererAPI = CreateScope<OpenGLRendererAPI>();
        break;
    }
    case RendererAPI::API::DirectX11:
    {
        //s_RendererAPI = CreateScope<DX11RendererAPI>();
        break;
    }
    }
#endif
    s_RendererAPI->Init();
}

void RenderCommand::SetClearColor(float r, float g, float b, float a)
{
    if (!s_RendererAPI) {
        return;
    }
    s_RendererAPI->SetClearColor(r, g, b, a);
}

void RenderCommand::Clear()
{
    if (!s_RendererAPI) {
        return;
    }
    s_RendererAPI->Clear();
}

void RenderCommand::DrawIndexed(const Ref<VertexArray>& vertexArray)
{
    if (!s_RendererAPI) {
        return;
    }
    s_RendererAPI->DrawIndexed(vertexArray);
}

}