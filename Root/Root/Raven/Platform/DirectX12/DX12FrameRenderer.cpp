#include "DX12FrameRenderer.h"

#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12Fence.h"
#include "DX12SwapChain.h"

namespace Raven
{

bool DX12FrameRenderer::Init(ID3D12Device* device, DX12SwapChain& swapChain)
{
    Shutdown();
    if (device == nullptr || swapChain.IsValid() == false) { return false; }

    D3D12_DESCRIPTOR_HEAP_DESC description{};
    description.NumDescriptors = DX12SwapChain::BufferCount;
    description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    if (FAILED(device->CreateDescriptorHeap(
        &description, IID_PPV_ARGS(m_RtvHeap.ReleaseAndGetAddressOf())))) { return false; }

    D3D12_DESCRIPTOR_HEAP_DESC dsvDescription{};
    dsvDescription.NumDescriptors = 1;
    dsvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(device->CreateDescriptorHeap(&dsvDescription,
        IID_PPV_ARGS(m_DsvHeap.ReleaseAndGetAddressOf()))))
    {
        Shutdown();
        return false;
    }

    m_RtvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return RebuildRenderTargets(device, swapChain);
}

bool DX12FrameRenderer::RebuildRenderTargets(ID3D12Device* device, DX12SwapChain& swapChain)
{
    if (device == nullptr || swapChain.IsValid() == false || m_RtvHeap.Get() == nullptr) { return false; }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    const auto& backBuffers = swapChain.GetBackBuffers();
    for (UINT index = 0; index < DX12SwapChain::BufferCount; ++index)
    {
        device->CreateRenderTargetView(backBuffers[index].Get(), nullptr, handle);
        handle.ptr += m_RtvDescriptorSize;
    }
    // Resize時は旧Depth ResourceをGPU完了後に差し替えます。
    // SceneとClear Demoで同じDSVを使い、BackBufferのサイズに一致させます。
    const D3D12_RESOURCE_DESC backBufferDesc = backBuffers[0]->GetDesc();
    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = backBufferDesc.Width;
    depthDesc.Height = backBufferDesc.Height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    if (FAILED(device->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(depth.GetAddressOf()))))
    {
        return false;
    }
    device->CreateDepthStencilView(depth.Get(), nullptr,
        m_DsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_DepthBuffer = std::move(depth);
    return true;
}

bool DX12FrameRenderer::BeginFrame(
    DX12SwapChain& swapChain, DX12CommandList& commandList,
    DX12Fence& fence, uint64_t frameFenceValue)
{
    if (m_FrameActive == true || swapChain.IsValid() == false ||
        commandList.IsValid() == false || fence.IsValid() == false ||
        m_RtvHeap.Get() == nullptr)
    {
        return false;
    }

    // このSlotの前回GPU処理が完了してからAllocatorをResetします。
    if (fence.Wait(frameFenceValue) == false || commandList.Reset() == false)
    {
        return false;
    }

    m_BackBufferIndex = swapChain.GetCurrentBackBufferIndex();
    if (m_BackBufferIndex >= swapChain.GetBackBuffers().size())
    {
        return false;
    }
    m_FrameActive = true;
    m_RenderTargetActive = false;
    m_RenderTargetFinished = false;
    m_Submitted = false;
    return true;
}

bool DX12FrameRenderer::BeginRenderTarget(
    DX12SwapChain& swapChain, DX12CommandList& commandList)
{
    if (m_FrameActive == false || m_RenderTargetActive == true ||
        m_RenderTargetFinished == true || m_Submitted == true ||
        commandList.IsValid() == false || swapChain.IsValid() == false ||
        m_RtvHeap.Get() == nullptr ||
        m_BackBufferIndex >= swapChain.GetBackBuffers().size())
    {
        return false;
    }

    ID3D12Resource* backBuffer = swapChain.GetBackBuffers()[m_BackBufferIndex].Get();
    if (backBuffer == nullptr)
    {
        return false;
    }

    // Clear Demoと通常Sceneで同じBackBuffer遷移を使用します。
    // Draw中にPRESENTへ戻すとRTVへの書き込みが不正になるため、終了時まで保持します。
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList.GetHandle()->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(m_BackBufferIndex) * m_RtvDescriptorSize;
    if (m_DsvHeap.Get() == nullptr || m_DepthBuffer.Get() == nullptr)
    {
        return false;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList.GetHandle()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    // Depthは毎Frame初期化し、前Frameの深度値を持ち越しません。
    commandList.GetHandle()->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_RenderTargetActive = true;
    return true;
}

bool DX12FrameRenderer::EndRenderTarget(
    DX12SwapChain& swapChain, DX12CommandList& commandList)
{
    if (m_FrameActive == false || m_RenderTargetActive == false ||
        m_Submitted == true || commandList.IsValid() == false ||
        swapChain.IsValid() == false ||
        m_BackBufferIndex >= swapChain.GetBackBuffers().size())
    {
        return false;
    }

    ID3D12Resource* backBuffer = swapChain.GetBackBuffers()[m_BackBufferIndex].Get();
    if (backBuffer == nullptr)
    {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList.GetHandle()->ResourceBarrier(1, &barrier);

    m_RenderTargetActive = false;
    m_RenderTargetFinished = true;
    return true;
}

bool DX12FrameRenderer::ClearRenderTarget(
    DX12CommandList& commandList, const float clearColor[4])
{
    if (m_FrameActive == false || m_RenderTargetActive == false ||
        m_Submitted == true || clearColor == nullptr ||
        commandList.IsValid() == false || m_RtvHeap.Get() == nullptr)
    {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(m_BackBufferIndex) * m_RtvDescriptorSize;
    commandList.GetHandle()->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    return true;
}

bool DX12FrameRenderer::ClearFrame(
    DX12SwapChain& swapChain, DX12CommandList& commandList,
    const float clearColor[4])
{
    if (clearColor == nullptr ||
        BeginRenderTarget(swapChain, commandList) == false)
    {
        return false;
    }

    // Clear Demoは従来どおりClearFrame内でRenderTargetを閉じます。
    if (ClearRenderTarget(commandList, clearColor) == false)
    {
        return false;
    }
    return EndRenderTarget(swapChain, commandList);
}

bool DX12FrameRenderer::EndFrame(
    DX12CommandQueue& commandQueue, DX12CommandList& commandList)
{
    if (m_FrameActive == false || m_RenderTargetFinished == false || m_RenderTargetActive == true ||
        m_Submitted == true || commandQueue.IsValid() == false ||
        commandList.IsValid() == false)
    {
        return false;
    }

    if (commandList.Close() == false)
    {
        return false;
    }

    ID3D12CommandList* lists[] = { commandList.GetHandle() };
    commandQueue.GetHandle()->ExecuteCommandLists(1, lists);
    m_Submitted = true;
    return true;
}

bool DX12FrameRenderer::Present(
    DX12SwapChain& swapChain, DX12CommandQueue& commandQueue,
    DX12Fence& fence, uint64_t& frameFenceValue, bool vsync)
{
    if (m_FrameActive == false || m_Submitted == false ||
        swapChain.IsValid() == false || commandQueue.IsValid() == false ||
        fence.IsValid() == false)
    {
        return false;
    }

    const UINT syncInterval = vsync == true ? 1 : 0;
    const HRESULT presentResult = swapChain.GetHandle()->Present(syncInterval, 0);

    // Present失敗でもExecute済みCommandListはGPUで使用中の可能性があります。
    // QueueへFenceを積み、次回このSlotを再利用する時に完了を待ちます。
    uint64_t submittedFenceValue = 0;
    if (fence.Signal(commandQueue.GetHandle(), submittedFenceValue) == false)
    {
        return false;
    }
    frameFenceValue = submittedFenceValue;
    m_FrameActive = false;
    m_RenderTargetActive = false;
    m_RenderTargetFinished = false;
    m_Submitted = false;
    return SUCCEEDED(presentResult);
}

bool DX12FrameRenderer::DrawClearFrame(
    DX12SwapChain& swapChain, DX12CommandQueue& commandQueue,
    DX12CommandList& commandList, DX12Fence& fence, uint64_t& frameFenceValue,
    const float clearColor[4], bool vsync)
{
    if (BeginFrame(swapChain, commandList, fence, frameFenceValue) == false ||
        ClearFrame(swapChain, commandList, clearColor) == false ||
        EndFrame(commandQueue, commandList) == false)
    {
        return false;
    }
    return Present(swapChain, commandQueue, fence, frameFenceValue, vsync);
}

void DX12FrameRenderer::Shutdown()
{
    m_RtvDescriptorSize = 0;
    m_BackBufferIndex = 0;
    m_FrameActive = false;
    m_RenderTargetActive = false;
    m_RenderTargetFinished = false;
    m_Submitted = false;
    m_DepthBuffer.Reset();
    m_DsvHeap.Reset();
    m_RtvHeap.Reset();
}

} // namespace Raven
