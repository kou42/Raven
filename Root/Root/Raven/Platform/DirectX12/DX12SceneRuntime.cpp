#include "DX12SceneRuntime.h"

#include "DX12OffscreenRenderTarget.h"

#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"
#include "Raven/Renderer/RHI/RHISceneMeshRenderer.h"

namespace Raven
{

bool DX12SceneRuntime::CreateAuxiliaryUIWindow(Window& window)
{
    if (m_Initialized == false || m_Device == nullptr || &window == m_Window ||
        m_AuxiliaryWindows.find(&window) != m_AuxiliaryWindows.end())
    {
        return false;
    }
    auto state = CreateScope<AuxiliaryUIWindow>();
    if (state->Context.Init(window, &m_Context) == false)
    {
        return false;
    }
    state->Width = window.GetFramebufferWidth();
    state->Height = window.GetFramebufferHeight();
    m_AuxiliaryWindows.emplace(&window, std::move(state));
    return true;
}

void DX12SceneRuntime::DestroyAuxiliaryUIWindow(Window& window)
{
    // ContextのShutdownが共有QueueにFenceを投入して待機し、Native Windowより先に
    // BackBuffer/Descriptor/Allocatorを解放します。MainのDeviceやQueueは存続します。
    m_AuxiliaryWindows.erase(&window);
}

bool DX12SceneRuntime::RenderAuxiliaryUIWindow(Window& window, const UIDrawList& drawList)
{
    const auto found = m_AuxiliaryWindows.find(&window);
    if (m_Initialized == false || found == m_AuxiliaryWindows.end() ||
        m_Context.GetActiveCommandList() != nullptr)
    {
        return false;
    }
    const uint32_t width = window.GetFramebufferWidth();
    const uint32_t height = window.GetFramebufferHeight();
    if (window.GetState() == WindowState::Minimized || width == 0 || height == 0)
    {
        return true; // 最小化は失敗とせず、旧SwapChainを保って次の有効寸法を待ちます。
    }
    AuxiliaryUIWindow& state = *found->second;
    if (state.Width != width || state.Height != height)
    {
        if (state.Context.Resize(width, height) == false)
        {
            return false;
        }
        state.Width = width;
        state.Height = height;
    }
    PreparedExplicitUI prepared;
    // Mainと同一Device上のPSO/Textureを使用します。準備はどのCommandListも記録して
    // いない境界で行い、Mainが書いたViewportを同じQueueの後続CommandでSamplingします。
    if (state.Renderer.Prepare(*m_Device, drawList, width, height,
        m_DefaultTexture, m_UIPipeline, prepared) == false ||
        state.Context.BeginFrame() != RHIFrameResult::Success)
    {
        return false;
    }
    DX12SceneCommandList commands(state.Context);
    const bool drawn = state.Renderer.Draw(commands, m_UIPipeline, prepared);
    // Draw失敗時も記録中Frameを閉じます。失敗したWindowはApplicationがClose予約します。
    const bool ended = state.Context.EndFrame() == RHIFrameResult::Success;
    const bool presented = ended == true && state.Context.Present() == RHIFrameResult::Success;
    return drawn == true && presented == true;
}

bool DX12SceneRuntime::SupportsOffscreenSceneRendering() const
{
    return m_Initialized == true && m_Device != nullptr;
}

Ref<RHIOffscreenRenderTarget>
DX12SceneRuntime::CreateOffscreenRenderTarget(
    const RHIOffscreenRenderTargetSpecification& specification)
{
    if (SupportsOffscreenSceneRendering() == false)
    {
        return nullptr;
    }
    return m_Device->CreateOffscreenRenderTarget(specification);
}

bool DX12SceneRuntime::CaptureMainSceneFrame()
{
    if (SupportsOffscreenSceneRendering() == false ||
        m_PreparedFrame != nullptr ||
        m_Context.GetActiveCommandList() != nullptr)
    {
        return false;
    }

    auto prepared = CreateScope<Renderer::PreparedRHISceneFrame>();
    if (PrepareCurrentSceneFrame(*prepared) == false)
    {
        return false;
    }
    m_PreparedFrame = std::move(prepared);
    return true;
}

bool DX12SceneRuntime::CaptureOffscreenSceneFrame(
    const Ref<RHIOffscreenRenderTarget>& renderTarget)
{
    if (SupportsOffscreenSceneRendering() == false ||
        renderTarget == nullptr || renderTarget->IsValid() == false ||
        m_PreparedFrame == nullptr ||
        m_Context.GetActiveCommandList() != nullptr)
    {
        return false;
    }

    PreparedOffscreenSceneFrame prepared;
    prepared.RenderTarget = renderTarget;
    if (PrepareCurrentSceneFrame(prepared.SceneFrame) == false)
    {
        return false;
    }
    m_PreparedOffscreenFrames.push_back(std::move(prepared));
    return true;
}

bool DX12SceneRuntime::PrepareCurrentSceneFrame(
    Renderer::PreparedRHISceneFrame& outFrame)
{
    if (Renderer::PrepareRHISceneFrame(*m_Device, m_OpaquePipeline,
        m_TransparentPipeline, m_DefaultTexture,
        DX12ClipCorrection(), outFrame) == false ||
        Renderer::PrepareRHIDebugLines(*m_Device, m_DefaultTexture,
            m_DebugLinePipeline, DX12ClipCorrection(),
            outFrame.DebugLineItems) == false)
    {
        return false;
    }
    outFrame.DebugLinePipeline = m_DebugLinePipeline;
    return true;
}

bool DX12SceneRuntime::DrawPreparedOffscreenSceneFrame(
    RHISceneCommandList& commands,
    const PreparedOffscreenSceneFrame& prepared)
{
    const std::array<float, 4>& clearColor =
        prepared.RenderTarget->GetSpecification().ClearColor;
    if (commands.BeginOffscreenRenderTarget(
        prepared.RenderTarget, clearColor.data()) == false)
    {
        return false;
    }

    // 途中のDrawが失敗してもEndを試み、SwapChain Passを閉じたまま残しません。
    bool drawn = RHISceneMeshRenderer::Draw(
        commands, prepared.SceneFrame.OpaquePipeline,
        prepared.SceneFrame.TransparentPipeline,
        prepared.SceneFrame.Items);
    if (drawn == true &&
        prepared.SceneFrame.DebugLineItems.empty() == false)
    {
        drawn = prepared.SceneFrame.DebugLinePipeline != nullptr &&
            RHISceneMeshRenderer::Draw(
                commands, prepared.SceneFrame.DebugLinePipeline,
                prepared.SceneFrame.DebugLinePipeline,
                prepared.SceneFrame.DebugLineItems);
    }

    auto nativeTarget =
        std::dynamic_pointer_cast<DX12OffscreenRenderTarget>(
            prepared.RenderTarget);
    RHIEntityPickingRequest pickingRequest{};
    if (drawn == true && nativeTarget != nullptr &&
        nativeTarget->TakePendingEntityPickingRequest(pickingRequest) == true)
    {
        const bool beganPicking = commands.BeginEntityPickingPass(
            prepared.RenderTarget, pickingRequest);
        bool picked = false;
        bool endedPicking = false;
        if (beganPicking == true)
        {
            picked = RHISceneMeshRenderer::DrawEntityPicking(
                commands, m_PickingPipeline, prepared.SceneFrame.Items);
            // Draw失敗時もAttachmentをCOPY_SOURCEから戻すためEndを必ず試みます。
            endedPicking = commands.EndEntityPickingPass(
                prepared.RenderTarget, pickingRequest);
        }
        drawn = beganPicking == true && picked == true && endedPicking == true;
    }

    const bool ended =
        commands.EndOffscreenRenderTarget(prepared.RenderTarget);
    return drawn == true && ended == true;
}

} // namespace Raven
