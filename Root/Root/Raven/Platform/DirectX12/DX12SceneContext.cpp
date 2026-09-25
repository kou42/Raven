#include "DX12SceneContext.h"
#include "DX12SceneRHIBuffer.h"
#include "DX12SceneRHITexture.h"

#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

#include <climits>
#include <iostream>

namespace Raven
{
DX12SceneContext::~DX12SceneContext()
{
    Shutdown();
}

bool DX12SceneContext::Init(Window& window)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::DirectX12 ||
        window.GetPlatformWindowHandle() == nullptr ||
        window.GetNativeWindow() == nullptr)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(window.GetNativeWindow()), &width, &height);
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    if (m_Factory.Init() == false ||
        m_Adapters.Enumerate(m_Factory.GetHandle()) == false)
    {
        Shutdown();
        return false;
    }

    // 列挙順ではDevice生成の可否が決まらないため、既存Clear Demoと同じ選択規則を使います。
    bool deviceCreated = false;
    for (const auto& adapter : m_Adapters.GetAdapters())
    {
        if (m_Device.Init(adapter) == true)
        {
            deviceCreated = true;
            break;
        }
    }
    if (deviceCreated == false ||
        m_Queue.Init(m_Device.GetHandle()) == false ||
        m_SwapChain.Init(m_Factory.GetHandle(), m_Queue.GetHandle(),
            m_Device.GetHandle(), window.GetPlatformWindowHandle(),
            static_cast<uint32_t>(width), static_cast<uint32_t>(height)) == false ||
        m_Fence.Init(m_Device.GetHandle()) == false ||
        m_FrameRenderer.Init(m_Device.GetHandle(), m_SwapChain) == false)
    {
        Shutdown();
        return false;
    }

    // BackBufferに対応するAllocatorをFence完了後にのみResetします。
    for (uint32_t index = 0; index < DX12SwapChain::BufferCount; ++index)
    {
        FrameResource frame;
        frame.CommandList = std::make_unique<DX12CommandList>();
        if (frame.CommandList->Init(m_Device.GetHandle()) == false)
        {
            Shutdown();
            return false;
        }
        m_Frames.push_back(std::move(frame));
    }

    m_CurrentFrame = 0;
    m_VSync = window.IsVSync();
    m_Device.DrainDebugMessages();
    return true;
}

RHIFrameResult DX12SceneContext::BeginFrame()
{
    if (m_FrameActive == true || m_CurrentFrame >= m_Frames.size() ||
        m_Frames[m_CurrentFrame].CommandList == nullptr)
    {
        return RHIFrameResult::FatalError;
    }

    FrameResource& frame = m_Frames[m_CurrentFrame];
    if (m_FrameRenderer.BeginFrame(m_SwapChain, *frame.CommandList,
        m_Fence, frame.FenceValue) == false)
    {
        m_Device.DrainDebugMessages();
        return RHIFrameResult::FatalError;
    }

    // BeginFrameのFence待機後は前回このFrameが参照したBufferを解放できます。
    // GPUがまだ読むBufferをEntity側のRef破棄だけで解放しないための保持です。
    frame.RetainedBuffers.clear();
    frame.RetainedPipelines.clear();
    frame.RetainedTextures.clear();

    // BeginFrame以降の失敗では記録中のCommandListが残るため、同Contextを再利用しません。
    m_FrameActive = true;
    m_FrameSubmitted = false;
    m_GraphicsPipelineBound = false;
    if (m_FrameRenderer.BeginRenderTarget(m_SwapChain, *frame.CommandList) == false ||
        m_FrameRenderer.ClearRenderTarget(*frame.CommandList, m_ClearColor) == false)
    {
        m_Device.DrainDebugMessages();
        return RHIFrameResult::FatalError;
    }
    // CommandList Reset後はRasterizer stateを毎Frame記録します。
    const auto& backBuffers = m_SwapChain.GetBackBuffers();
    const UINT backBufferIndex = m_SwapChain.GetCurrentBackBufferIndex();
    if (backBufferIndex >= backBuffers.size() || backBuffers[backBufferIndex] == nullptr)
    {
        return RHIFrameResult::FatalError;
    }
    const D3D12_RESOURCE_DESC description = backBuffers[backBufferIndex]->GetDesc();
    if (SetViewport(0, 0, static_cast<uint32_t>(description.Width), description.Height) == false)
    {
        return RHIFrameResult::FatalError;
    }
    return RHIFrameResult::Success;
}

RHIFrameResult DX12SceneContext::EndFrame()
{
    if (m_FrameActive == false || m_FrameSubmitted == true ||
        m_CurrentFrame >= m_Frames.size())
    {
        return RHIFrameResult::FatalError;
    }

    DX12CommandList& commandList = *m_Frames[m_CurrentFrame].CommandList;
    if (m_FrameRenderer.EndRenderTarget(m_SwapChain, commandList) == false ||
        m_FrameRenderer.EndFrame(m_Queue, commandList) == false)
    {
        m_Device.DrainDebugMessages();
        return RHIFrameResult::FatalError;
    }
    m_FrameSubmitted = true;
    return RHIFrameResult::Success;
}

RHIFrameResult DX12SceneContext::Present()
{
    if (m_FrameActive == false || m_FrameSubmitted == false)
    {
        return RHIFrameResult::FatalError;
    }

    FrameResource& frame = m_Frames[m_CurrentFrame];
    const bool presented = m_FrameRenderer.Present(
        m_SwapChain, m_Queue, m_Fence, frame.FenceValue, m_VSync);
    m_Device.DrainDebugMessages();
    if (presented == false)
    {
        // Execute後のPresent/Signal失敗はFatalとして扱い、Contextを再生成します。
        return RHIFrameResult::FatalError;
    }

    m_FrameActive = false;
    m_FrameSubmitted = false;
    m_GraphicsPipelineBound = false;
    m_CurrentFrame = (m_CurrentFrame + 1) % static_cast<uint32_t>(m_Frames.size());
    return RHIFrameResult::Success;
}

bool DX12SceneContext::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || m_FrameActive == true ||
        m_SwapChain.IsValid() == false || m_Queue.IsValid() == false)
    {
        return false;
    }

    // ResizeBuffers前に旧BackBufferを参照するGPU Commandをすべて完了させます。
    if (m_Fence.SignalAndWait(m_Queue.GetHandle()) == false ||
        m_SwapChain.Resize(width, height) == false ||
        m_FrameRenderer.RebuildRenderTargets(m_Device.GetHandle(), m_SwapChain) == false)
    {
        m_Device.DrainDebugMessages();
        return false;
    }
    m_CurrentFrame = 0;
    m_Device.DrainDebugMessages();
    return true;
}

bool DX12SceneContext::SetViewport(
    uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    ID3D12GraphicsCommandList* commandList = GetActiveCommandList();
    if (commandList == nullptr || width == 0 || height == 0 ||
        static_cast<uint64_t>(x) + width > static_cast<uint64_t>(LONG_MAX) ||
        static_cast<uint64_t>(y) + height > static_cast<uint64_t>(LONG_MAX))
    {
        return false;
    }

    // DX12のViewport/ScissorはDraw時のCommandList stateとして記録します。
    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{};
    scissor.left = static_cast<LONG>(x);
    scissor.top = static_cast<LONG>(y);
    scissor.right = static_cast<LONG>(x + width);
    scissor.bottom = static_cast<LONG>(y + height);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    return true;
}

bool DX12SceneContext::ClearColorAttachment(const float color[4])
{
    if (color == nullptr || GetActiveCommandList() == nullptr ||
        m_CurrentFrame >= m_Frames.size() ||
        m_Frames[m_CurrentFrame].CommandList == nullptr)
    {
        return false;
    }

    // BeginFrameのClear色設定とは異なり、現在のCommandListに即時記録します。
    // FrameRenderer側でRenderTargetActiveかどうかも検証します。
    return m_FrameRenderer.ClearRenderTarget(
        *m_Frames[m_CurrentFrame].CommandList, color);
}

void DX12SceneContext::SetClearColor(const float color[4])
{
    if (color == nullptr)
    {
        return;
    }
    for (uint32_t component = 0; component < 4; ++component)
    {
        m_ClearColor[component] = color[component];
    }
}

ID3D12GraphicsCommandList* DX12SceneContext::GetActiveCommandList() const
{
    if (m_FrameActive == false || m_FrameSubmitted == true ||
        m_CurrentFrame >= m_Frames.size() ||
        m_Frames[m_CurrentFrame].CommandList == nullptr)
    {
        return nullptr;
    }
    return m_Frames[m_CurrentFrame].CommandList->GetHandle();
}

ID3D12Device* DX12SceneContext::GetNativeDevice() const
{
    return m_Device.GetHandle();
}

bool DX12SceneContext::RetainDrawBuffers(
    const Ref<RHIBuffer>& vertexBuffer, const Ref<RHIBuffer>& indexBuffer)
{
    if (GetActiveCommandList() == nullptr ||
        vertexBuffer == nullptr || indexBuffer == nullptr)
    {
        return false;
    }
    FrameResource& frame = m_Frames[m_CurrentFrame];
    frame.RetainedBuffers.push_back(vertexBuffer);
    frame.RetainedBuffers.push_back(indexBuffer);
    return true;
}

bool DX12SceneContext::BindGraphicsPipeline(
    ID3D12PipelineState* pipelineState, ID3D12RootSignature* rootSignature)
{
    ID3D12GraphicsCommandList* commandList = GetActiveCommandList();
    if (commandList == nullptr || pipelineState == nullptr ||
        rootSignature == nullptr)
    {
        return false;
    }

    // CommandListのResetでPSO/Root Signatureは引き継がれません。
    // 毎Frame明示的にBindしてからIndexed Drawを許可します。
    commandList->SetGraphicsRootSignature(rootSignature);
    commandList->SetPipelineState(pipelineState);
    m_GraphicsPipelineBound = true;
    return true;
}

bool DX12SceneContext::RetainGraphicsPipeline(
    const Ref<RHIGraphicsPipeline>& pipeline)
{
    if (GetActiveCommandList() == nullptr || pipeline == nullptr ||
        m_GraphicsPipelineBound == false)
    {
        return false;
    }
    // PSO/Root Signatureも記録済みGPU命令が参照するためFence完了まで保持します。
    m_Frames[m_CurrentFrame].RetainedPipelines.push_back(pipeline);
    return true;
}

bool DX12SceneContext::IsGraphicsPipelineBound() const
{
    return GetActiveCommandList() != nullptr && m_GraphicsPipelineBound == true;
}

bool DX12SceneContext::SetClipTransform(
    const std::array<float, 16>& transform)
{
    ID3D12GraphicsCommandList* commandList = GetActiveCommandList();
    if (commandList == nullptr || m_GraphicsPipelineBound == false)
    {
        return false;
    }
    // Root Signatureのb0へcolumn-major行列を16 DWORDで記録します。
    commandList->SetGraphicsRoot32BitConstants(
        0, static_cast<UINT>(transform.size()), transform.data(), 0);
    return true;
}

bool DX12SceneContext::SetMaterialTint(
    const std::array<float, 4>& tint)
{
    ID3D12GraphicsCommandList* commandList = GetActiveCommandList();
    if (commandList == nullptr || m_GraphicsPipelineBound == false)
    {
        return false;
    }
    // Root Signatureのb1へRGBAを4 DWORDで記録します。
    commandList->SetGraphicsRoot32BitConstants(
        1, static_cast<UINT>(tint.size()), tint.data(), 0);
    return true;
}

bool DX12SceneContext::BindTexture(const Ref<RHITexture>& texture)
{
    ID3D12GraphicsCommandList* commandList = GetActiveCommandList();
    auto native = std::dynamic_pointer_cast<DX12SceneRHITexture>(texture);
    if (commandList == nullptr || m_GraphicsPipelineBound == false ||
        native == nullptr || native->GetOwnerDevice() != GetNativeDevice() ||
        native->GetNativeTexture() == nullptr || native->GetSrvHeap() == nullptr)
    {
        return false;
    }

    // Shader-visible Heapは同時に1つだけBindできます。
    // 描画ごとにTexture固有Heapへ切り替え、t0のDescriptor Tableを再設定します。
    ID3D12DescriptorHeap* heaps[] = { native->GetSrvHeap() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootDescriptorTable(2, native->GetSrvGpuHandle());
    // Texture本体とDescriptor HeapをGPUのFence完了まで保持します。
    m_Frames[m_CurrentFrame].RetainedTextures.push_back(texture);
    return true;
}

bool DX12SceneContext::DrawIndexed(
    const Ref<RHIBuffer>& vertexBuffer,
    const Ref<RHIBuffer>& indexBuffer,
    uint32_t stride, uint32_t indexCount)
{
    ID3D12GraphicsCommandList* commandList = GetActiveCommandList();
    if (commandList == nullptr || m_GraphicsPipelineBound == false ||
        vertexBuffer == nullptr || indexBuffer == nullptr || stride == 0 ||
        vertexBuffer->GetSpecification().Usage != RHIBufferUsage::Vertex ||
        indexBuffer->GetSpecification().Usage != RHIBufferUsage::Index)
    {
        return false;
    }

    // 別BackendのRHIBufferや別種Resourceをnative APIへ渡さないようにします。
    auto vertex = std::dynamic_pointer_cast<DX12SceneRHIBuffer>(vertexBuffer);
    auto index = std::dynamic_pointer_cast<DX12SceneRHIBuffer>(indexBuffer);
    if (vertex == nullptr || index == nullptr ||
        vertex->GetSceneBuffer().IsValid() == false ||
        index->GetSceneBuffer().IsValid() == false ||
        vertex->GetSceneBuffer().IsIndexBuffer() == true ||
        index->GetSceneBuffer().IsIndexBuffer() == false ||
        vertex->GetOwnerDevice() != GetNativeDevice() ||
        index->GetOwnerDevice() != GetNativeDevice())
    {
        return false;
    }

    const D3D12_VERTEX_BUFFER_VIEW& originalVertexView =
        vertex->GetSceneBuffer().GetVertexView();
    const D3D12_INDEX_BUFFER_VIEW& indexView =
        index->GetSceneBuffer().GetIndexView();
    const uint32_t availableIndices = index->GetSceneBuffer().GetIndexCount();
    const uint32_t drawCount = indexCount == 0 ? availableIndices : indexCount;
    if (drawCount == 0 || drawCount > availableIndices ||
        originalVertexView.SizeInBytes < stride ||
        originalVertexView.SizeInBytes % stride != 0 ||
        drawCount > indexView.SizeInBytes / sizeof(uint32_t))
    {
        return false;
    }

    // IASetVertexBuffersはViewの値を記録するため、Pipeline指定のstrideを反映した
    // ローカルViewを使用します。Buffer生成時のbyte単位strideは描画に流用しません。
    D3D12_VERTEX_BUFFER_VIEW vertexView = originalVertexView;
    vertexView.StrideInBytes = stride;

    // GPUが描画命令を消費するまで両Bufferを保持してから記録します。
    if (RetainDrawBuffers(vertexBuffer, indexBuffer) == false)
    {
        return false;
    }
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexView);
    commandList->IASetIndexBuffer(&indexView);
    commandList->DrawIndexedInstanced(drawCount, 1, 0, 0, 0);
    return true;
}

void DX12SceneContext::Shutdown()
{
    if (m_Fence.IsValid() == true && m_Queue.IsValid() == true)
    {
        if (m_Fence.SignalAndWait(m_Queue.GetHandle()) == false)
        {
            // Device Removed等ではGPU完了を保証できません。終了処理は継続し、
            // native Resourceの解放失敗をDebug Layerで追跡できるようにします。
            std::cerr << "DX12 Scene Shutdown: GPU Fence wait failed; "
                "resource completion is not guaranteed.\n";
            m_Device.DrainDebugMessages();
        }
    }
    m_FrameActive = false;
    m_FrameSubmitted = false;
    m_GraphicsPipelineBound = false;
    m_CurrentFrame = 0;
    // Fence待機を試みた後にFrame保持Buffer/PSO/Texture参照を先に解放します。
    // FrameRenderer・SwapChain・Deviceの破棄後まで旧Resourceを保持しません。
    m_Frames.clear();
    m_FrameRenderer.Shutdown();
    m_SwapChain.Shutdown();
    m_Fence.Shutdown();
    m_Queue.Shutdown();
    m_Device.Shutdown();
    m_Adapters.Clear();
    m_Factory.Shutdown();
}
} // namespace Raven
