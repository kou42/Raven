#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"

namespace Raven
{

Scope<RendererAPI> RenderCommand::s_RendererAPI = nullptr;

// ダミーのRendererAPIを作成しておくことで、RenderCommandの呼び出しがRendererAPIの初期化前に行われてもクラッシュしないようにする
Scope<RendererAPI> s_dummyRendererAPI = CreateScope<OpenGLRendererAPI>();

void RenderCommand::SetAPI(
    std::unique_ptr<RendererAPI> api
)
{
    s_RendererAPI = std::move(api);
}

void RenderCommand::Init()
{
    if (s_RendererAPI) {
        return;
    }

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

void RenderCommand::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!s_RendererAPI) {
        assert(s_RendererAPI);
        return;
    }
    s_RendererAPI->SetViewport(x, y, width, height);
}

void RenderCommand::SetClearColor(float r, float g, float b, float a)
{
    if (!s_RendererAPI) {
        assert(s_RendererAPI);
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

void RenderCommand::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
    if (!s_RendererAPI) {
        assert(s_RendererAPI);
        return;
    }
    s_RendererAPI->DrawIndexed(vertexArray, indexCount);
}

RendererAPI& RenderCommand::GetAPI()
{
    if (!s_RendererAPI) {
        assert(s_RendererAPI);
        return *s_dummyRendererAPI;
    }

    return *s_RendererAPI;
}

}