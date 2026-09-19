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
    bool DrawClearFrame(DX12SwapChain& swapChain, DX12CommandQueue& commandQueue,
        DX12CommandList& commandList, DX12Fence& fence, uint64_t& frameFenceValue,
        const float clearColor[4], bool vsync);
    void Shutdown();

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    UINT m_RtvDescriptorSize = 0;
};
} // namespace Raven
