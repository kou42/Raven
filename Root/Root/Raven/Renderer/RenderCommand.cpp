#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Renderer.h"

namespace Raven
{

// GPU Resource生成はDevice、描画命令はCommandListへ責務を分離します。
// Legacy RendererAPI objectはRHI移行完了に伴い削除済みで、RenderCommandはこの2つを
// Renderer上位層から利用するための共通窓口として保持します。
Scope<RHIDevice> RenderCommand::s_Device = nullptr;
Scope<RHICommandList> RenderCommand::s_CommandList = nullptr;

// Draw統計では実際にBindされているPipelineのPrimitiveTopologyが必要です。
// CommandListへBindしたPipelineをRenderCommand側でも追跡し、Line / Point描画を
// Triangleとして誤計上しないために利用します。
Ref<Pipeline> RenderCommand::s_CurrentPipeline = nullptr;

void RenderCommand::Init()
{
    // Backend選択と実際の描画object生成をRHI層の識別子へ統一します。
    // Renderer上位層やLegacy RendererAPIを経由せず、RHIDevice / RHICommandListを直接構築します。
    //
    // 現段階ではOpenGL Backendのみ実装済みです。未実装BackendをOpenGLへ暗黙fallbackすると、
    // 将来のBackend追加時に設定ミスを見逃すため、未対応Backendではobjectを生成しません。
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
    {
        s_Device = CreateScope<OpenGLRHIDevice>();
        s_CommandList = CreateScope<OpenGLRHICommandList>();
        break;
    }
    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        break;
    }

    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    // Graphics ContextはApplication/Platform層で既に生成済みです。
    // CommandList::Init()ではBackend固有の初期描画stateだけを設定します。
    // 旧RendererAPI::Init()が担当していた初期stateも、このRHI Backend境界へ移行しています。
    s_CommandList->Init();
}

void RenderCommand::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    // 上位RendererはGraphics API固有のViewport命令を発行せず、CommandListへ委譲します。
    s_CommandList->SetViewport(x, y, width, height);
}

RHIViewport RenderCommand::GetViewport()
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return {};
    }

    // Physics Debug Overlay等が現在の描画領域を取得する際も、GL_VIEWPORTへ直接依存させません。
    return s_CommandList->GetViewport();
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

    // PipelineのBackend適用自体はCommandListへ任せます。
    // RenderCommand側ではDraw統計に必要なTopology参照のため、最後にBindしたPipelineだけ保持します。
    s_CommandList->BindPipeline(pipeline);
    s_CurrentPipeline = pipeline;
}

void RenderCommand::BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot)
{
    if (s_CommandList == nullptr || texture == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    // Shader uniform名とTexture slotの関連付けを含むBackend処理はCommandListへ閉じ込めます。
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

    // IndexBufferが存在しない、または空の場合は実Draw Callも発行しません。
    // 統計だけを増加させる状態を避け、Renderer StatisticsとGPU命令の対応を維持します。
    if (resolvedIndexCount == 0)
    {
        return;
    }

    // Pipeline未指定の旧描画経路は従来どおりTriangle Listとして扱います。
    // Material / Physics Debug等のPipeline経路では実Topologyを使うため、Lines/Pointsを
    // TriangleCountへ誤計上せずDrawCallsとIndexCountだけへ反映できます。
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    if (s_CurrentPipeline != nullptr)
    {
        topology = s_CurrentPipeline->GetSpecification().Topology;
    }

    // Renderer側にはAPI非依存の統計だけを記録し、実際のDraw命令とTopology変換は
    // RHICommandList -> Graphics Backendへ委譲します。
    Renderer::RecordIndexedDraw(resolvedIndexCount, topology);
    s_CommandList->DrawIndexed(vertexArray, indexCount);
}

RHIDevice* RenderCommand::GetDevice()
{
    // Legacy Resource BridgeがGPU Resource生成をRHIDeviceへ委譲するための参照です。
    // 所有権はRenderCommandが保持し、呼び出し側へは非所有pointerだけを公開します。
    return s_Device.get();
}

}
