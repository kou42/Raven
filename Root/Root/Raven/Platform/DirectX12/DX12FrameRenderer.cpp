#include "DX12FrameRenderer.h"

#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12Fence.h"
#include "DX12SwapChain.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Raven
{

bool DX12FrameRenderer::Init(ID3D12Device* device, DX12SwapChain& swapChain)
{
    Shutdown();

    if (device == nullptr || swapChain.IsValid() == false)
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.NumDescriptors = DX12SwapChain::BufferCount;
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT result = device->CreateDescriptorHeap(
        &heapDescription,
        IID_PPV_ARGS(m_RtvHeap.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        return false;
    }

    m_RtvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_RtvHeap->GetCPUDescriptorHandleForHeapStart();

    const auto& backBuffers = swapChain.GetBackBuffers();
    for (UINT index = 0; index < DX12SwapChain::BufferCount; ++index)
    {
        device->CreateRenderTargetView(backBuffers[index].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_RtvDescriptorSize;
    }

    return true;
}

bool DX12FrameRenderer::DrawClearFrame(
    DX12SwapChain& swapChain,
    DX12CommandQueue& commandQueue,
    DX12CommandList& commandList,
    DX12Fence& fence,
    const float clearColor[4],
    bool vsync)
{
    if (swapChain.IsValid() == false ||
        commandQueue.IsValid() == false ||
        commandList.IsValid() == false ||
        fence.IsValid() == false ||
        m_RtvHeap.Get() == nullptr)
    {
        return false;
    }

    if (commandList.Reset() == false)
    {
        return false;
    }

    const UINT backBufferIndex = swapChain.GetCurrentBackBufferIndex();
    ID3D12Resource* backBuffer = swapChain.GetBackBuffers()[backBufferIndex].Get();

    D3D12_RESOURCE_BARRIER toRenderTarget{};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toRenderTarget.Transition.pResource = backBuffer;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList.GetHandle()->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(backBufferIndex) * m_RtvDescriptorSize;

    commandList.GetHandle()->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    commandList.GetHandle()->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList.GetHandle()->ResourceBarrier(1, &toPresent);

    if (commandList.Close() == false)
    {
        return false;
    }

    ID3D12CommandList* commandLists[] = { commandList.GetHandle() };
    commandQueue.GetHandle()->ExecuteCommandLists(1, commandLists);

    const UINT syncInterval = vsync == true ? 1 : 0;
    const HRESULT result = swapChain.GetHandle()->Present(syncInterval, 0);
    if (FAILED(result))
    {
        return false;
    }

    // 現段階ではFrameごとに待つことでAllocator再利用を確実に安全にします。
    // 後でFrame Resourcesを複数持たせると、CPU/GPUを並列に進められます。
    return fence.SignalAndWait(commandQueue.GetHandle());
}

void DX12FrameRenderer::Shutdown()
{
    m_RtvDescriptorSize = 0;
    m_RtvHeap.Reset();
}

} // namespace Raven
