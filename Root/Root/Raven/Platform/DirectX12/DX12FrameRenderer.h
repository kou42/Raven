#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Raven
{
class DX12CommandList;
class DX12CommandQueue;
class DX12Fence;
class DX12SwapChain;

class DX12FrameRenderer
{
public:
    bool Init(ID3D12Device* device, DX12SwapChain& swapChain);
    bool RebuildRenderTargets(ID3D12Device* device, DX12SwapChain& swapChain);
    bool BeginFrame(DX12SwapChain& swapChain, DX12CommandList& commandList,
        DX12Fence& fence, uint64_t frameFenceValue);
    // Scene描画中はBackBufferをRENDER_TARGETに維持し、Draw完了後にPRESENTへ戻します。
    // Clear DemoはClearFrame互換入口から同じ遷移を使用します。
    bool BeginRenderTarget(DX12SwapChain& swapChain, DX12CommandList& commandList);
    bool EndRenderTarget(DX12SwapChain& swapChain, DX12CommandList& commandList);
    // RenderTargetを開いたままClearします。Scene Drawはこの後に記録します。
    bool ClearRenderTarget(DX12CommandList& commandList, const float clearColor[4]);
    bool ClearFrame(DX12SwapChain& swapChain, DX12CommandList& commandList,
        const float clearColor[4]);
    bool EndFrame(DX12CommandQueue& commandQueue, DX12CommandList& commandList);
    bool Present(DX12SwapChain& swapChain, DX12CommandQueue& commandQueue,
        DX12Fence& fence, uint64_t& frameFenceValue, bool vsync);
    bool DrawClearFrame(DX12SwapChain& swapChain, DX12CommandQueue& commandQueue,
        DX12CommandList& commandList, DX12Fence& fence, uint64_t& frameFenceValue,
        const float clearColor[4], bool vsync);
    void Shutdown();

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    UINT m_RtvDescriptorSize = 0;
    UINT m_BackBufferIndex = 0;
    bool m_FrameActive = false;
    bool m_RenderTargetActive = false;
    bool m_RenderTargetFinished = false;
    bool m_Submitted = false;
};
} // namespace Raven
