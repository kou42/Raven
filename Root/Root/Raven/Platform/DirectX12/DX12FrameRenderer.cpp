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

bool DX12FrameRenderer::DrawClearFrame(
    DX12SwapChain& swapChain, DX12CommandQueue& commandQueue,
    DX12CommandList& commandList, DX12Fence& fence,
    const float clearColor[4], bool vsync)
{
    if (swapChain.IsValid() == false || commandQueue.IsValid() == false ||
        commandList.IsValid() == false || fence.IsValid() == false ||
        m_RtvHeap.Get() == nullptr) { return false; }

    if (commandList.Reset() == false) { return false; }

    const UINT index = swapChain.GetCurrentBackBufferIndex();
    ID3D12Resource* backBuffer = swapChain.GetBackBuffers()[index].Get();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList.GetHandle()->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * m_RtvDescriptorSize;
    commandList.GetHandle()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList.GetHandle()->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList.GetHandle()->ResourceBarrier(1, &barrier);

    if (commandList.Close() == false) { return false; }

    ID3D12CommandList* lists[] = { commandList.GetHandle() };
    commandQueue.GetHandle()->ExecuteCommandLists(1, lists);

    const UINT syncInterval = vsync == true ? 1 : 0;
    const HRESULT presentResult = swapChain.GetHandle()->Present(syncInterval, 0);

    // Present失敗でもExecute済みCommandListはGPUで使用中の可能性があります。
    // 必ずFenceで完了を待ち、次FrameでAllocatorを早期Resetしないようにします。
    const bool completed = fence.SignalAndWait(commandQueue.GetHandle());
    return SUCCEEDED(presentResult) && completed == true;
}

void DX12FrameRenderer::Shutdown()
{
    m_RtvDescriptorSize = 0;
    m_RtvHeap.Reset();
}

} // namespace Raven
