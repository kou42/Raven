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
    const bool initialized = TryInit(GetRHIBackend());
    assert(initialized == true);
}

bool RenderCommand::TryInit(RHIBackend backend)
{
    // 再初期化時は旧Backendの参照を先に破棄し、異なるAPIのResourceを混在させません。
    s_CurrentPipeline.reset();
    s_CommandList.reset();
    s_Device.reset();

    // Backend選択と実際の描画object生成をRHI層の識別子へ統一します。
    // 現段階のLegacy CommandListはOpenGLのみです。未対応BackendをOpenGLへ暗黙fallbackせず、
    // 呼び出し側がExplicit Runtimeへ切り替えられるよう失敗を返します。
    switch (backend)
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

    if (s_Device == nullptr || s_CommandList == nullptr)
    {
        s_Device.reset();
        s_CommandList.reset();
        return false;
    }

    // Graphics ContextはApplication/Platform層で既に生成済みです。
    // CommandList::Init()ではBackend固有の初期描画stateだけを設定します。
    // 旧RendererAPI::Init()が担当していた初期stateも、このRHI Backend境界へ移行しています。
    s_CommandList->Init();
    return true;
}

void RenderCommand::BindRenderTarget(const Framebuffer& framebuffer)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->BindRenderTarget(framebuffer);
}

void RenderCommand::BindDefaultRenderTarget()
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->BindDefaultRenderTarget();
}

RHIRenderTargetState RenderCommand::CaptureRenderTargetState()
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return {};
    }

    return s_CommandList->CaptureRenderTargetState();
}

void RenderCommand::RestoreRenderTargetState(const RHIRenderTargetState& state)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->RestoreRenderTargetState(state);
}

void RenderCommand::SetViewport(int32_t x, int32_t y, uint32_t width, uint32_t height)
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

void RenderCommand::SetScissor(bool enabled, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    s_CommandList->SetScissor(enabled, x, y, width, height);
}

RHIScissor RenderCommand::GetScissor()
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return {};
    }

    return s_CommandList->GetScissor();
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

Ref<Pipeline> RenderCommand::GetBoundPipeline()
{
    return s_CurrentPipeline;
}

void RenderCommand::RestorePipelineBinding(const Ref<Pipeline>& pipeline)
{
    if (s_CommandList == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    // nullptrの場合も直前のUI Pipelineを残さず、次のDrawのTopology誤認を防ぎます。
    s_CommandList->RestorePipelineBinding(pipeline);
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

void RenderCommand::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t firstIndex)
{
    if (s_CommandList == nullptr || vertexArray == nullptr)
    {
        assert(s_CommandList);
        return;
    }

    // indexCount == 0 は「VAOのIndexBuffer全体を描画する」意味です。
    // 実際に発行されるIndex数へ解決してから統計へ記録することで、呼び出し経路によらず
    // StatisticsPanelの値を実Draw Callと一致させます。
    const Ref<IndexBuffer>& indexBuffer = vertexArray->GetIndexBuffer();
    if (indexBuffer == nullptr || firstIndex > indexBuffer->GetCount())
    {
        return;
    }

    // 減算で残り要素数を求め、offset + countの整数overflowを避けます。
    const uint32_t remainingCount = indexBuffer->GetCount() - firstIndex;
    const uint32_t resolvedIndexCount = indexCount == 0 ? remainingCount : indexCount;
    if (resolvedIndexCount == 0 || resolvedIndexCount > remainingCount)
    {
        return;
    }

    // 明示Index数がBuffer容量を超える場合はGPUへ不正なDrawを送らず、統計も増やしません。
    const Ref<IndexBuffer>& boundIndexBuffer = vertexArray->GetIndexBuffer();
    if (boundIndexBuffer == nullptr || resolvedIndexCount > boundIndexBuffer->GetCount())
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

    // None topologyではBackendがDrawを発行しないため、統計も記録しません。
    if (topology == PrimitiveTopology::None)
    {
        return;
    }

    // Renderer側にはAPI非依存の統計だけを記録し、実際のDraw命令とTopology変換は
    // RHICommandList -> Graphics Backendへ委譲します。
    if (topology == PrimitiveTopology::None)
    {
        return;
    }

    Renderer::RecordIndexedDraw(resolvedIndexCount, topology);
    s_CommandList->DrawIndexed(vertexArray, resolvedIndexCount, firstIndex);
}

RHIDevice* RenderCommand::GetDevice()
{
    // Legacy Resource BridgeがGPU Resource生成をRHIDeviceへ委譲するための参照です。
    // 所有権はRenderCommandが保持し、呼び出し側へは非所有pointerだけを公開します。
    return s_Device.get();
}

}
