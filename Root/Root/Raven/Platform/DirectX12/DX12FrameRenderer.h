#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace Raven
{

class DX12CommandList;
class DX12CommandQueue;
class DX12Fence;
class DX12SwapChain;

// BackBufferをRTVへ接続し、Clear -> Execute -> Present -> Fence待機まで実行します。
// CPU/GPU同期を単純化するため、学習段階では各Frame末尾でFence完了を待ちます。
class DX12FrameRenderer
{
public:
    DX12FrameRenderer() = default;
    ~DX12FrameRenderer() = default;

    bool Init(ID3D12Device* device, DX12SwapChain& swapChain);
    bool DrawClearFrame(
        DX12SwapChain& swapChain,
        DX12CommandQueue& commandQueue,
        DX12CommandList& commandList,
        DX12Fence& fence,
        const float clearColor[4],
        bool vsync);
    void Shutdown();

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    UINT m_RtvDescriptorSize = 0;
};

} // namespace Raven
