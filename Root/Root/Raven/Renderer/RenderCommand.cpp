#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Renderer.h"

namespace Raven
{

Scope<RendererAPI> RenderCommand::s_RendererAPI = nullptr;
Scope<RHIDevice> RenderCommand::s_Device = nullptr;
Scope<RHICommandList> RenderCommand::s_CommandList = nullptr;

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
    if (s_RendererAPI == nullptr)
    {
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
        case RendererAPI::API::DirectX12:
        {
            //s_RendererAPI = CreateScope<DX12RendererAPI>();
            break;
        }
        case RendererAPI::API::Vulkan:
        {
            //s_RendererAPI = CreateScope<VulkanRendererAPI>();
            break;
        }
        case RendererAPI::API::None:
        default:
            assert(false && "Renderer API is None");
            break;
        }
    }

    if (s_RendererAPI == nullptr)
    {
        return;
    }

    s_RendererAPI->Init();

    // RHI移行中もRendererAPI object自体は既存互換のため維持しますが、
    // GPU Resource生成はDevice、描画命令はCommandListへ責務を分離します。
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
    {
        s_Device = CreateScope<OpenGLRHIDevice>();
        s_CommandList = CreateScope<OpenGLRHICommandList>();
        break;
    }
    case RendererAPI::API::DirectX11:
    case RendererAPI::API::DirectX12:
    case RendererAPI::API::Vulkan:
    case RendererAPI::API::None:
    default:
        break;
    }
}

void RenderCommand::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->SetViewport(x, y, width, height);
}

void RenderCommand::SetClearColor(float r, float g, float b, float a)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->SetClearColor(r, g, b, a);
}

void RenderCommand::Clear()
{
    if (s_CommandList == nullptr)
    {
        return;
    }

    s_CommandList->Clear();
}

void RenderCommand::BindPipeline(const Ref<Pipeline>& pipeline)
{
    if (s_CommandList == nullptr || pipeline == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->BindPipeline(pipeline);
}

void RenderCommand::BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot)
{
    if (s_CommandList == nullptr || texture == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->BindTexture(name, texture, slot);
}

void RenderCommand::UploadUniform(const std::string& name, const UniformValue& value)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->UploadUniform(name, value);
}

void RenderCommand::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
    if (s_CommandList == nullptr || vertexArray == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    // indexCount == 0 は「VAOのIndexBuffer全体を描画する」意味です。
    // 実際に発行されるIndex数へ解決してから統計へ記録することで、呼び出し経路によらず
    // StatisticsPanelの値を実Draw Callと一致させます。
    uint32_t resolvedIndexCount = indexCount;
    if (resolvedIndexCount == 0)
    {
        const Ref<IndexBuffer>& indexBuffer = vertexArray->GetIndexBuffer();
        if (indexBuffer != nullptr)
        {
            resolvedIndexCount = indexBuffer->GetCount();
        }
    }

    Renderer::RecordIndexedDraw(resolvedIndexCount);
    s_CommandList->DrawIndexed(vertexArray, indexCount);
}

RHIDevice* RenderCommand::GetDevice()
{
    return s_Device.get();
}

RendererAPI& RenderCommand::GetAPI()
{
    if (s_RendererAPI == nullptr)
    {
        assert(s_RendererAPI);
        return *s_dummyRendererAPI;
    }

    return *s_RendererAPI;
}

}
