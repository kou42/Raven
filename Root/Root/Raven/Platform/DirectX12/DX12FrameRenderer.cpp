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
    commandList.GetHandle()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
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

bool DX12FrameRenderer::ClearFrame(
    DX12SwapChain& swapChain, DX12CommandList& commandList,
    const float clearColor[4])
{
    if (clearColor == nullptr ||
        BeginRenderTarget(swapChain, commandList) == false)
    {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(m_BackBufferIndex) * m_RtvDescriptorSize;
    commandList.GetHandle()->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
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
    m_RtvHeap.Reset();
}

} // namespace Raven
