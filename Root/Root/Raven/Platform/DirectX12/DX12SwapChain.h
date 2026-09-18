#pragma once

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

struct ID3D12CommandQueue;
struct ID3D12Device;
struct ID3D12Resource;

namespace Raven
{

// HWNDへPresentするFlip Model SwapChainを所有します。
// RTV Heap等はCommand List実装と合わせて追加し、⑩ではBackBuffer取得までを担当します。
class DX12SwapChain
{
public:
    static constexpr UINT BufferCount = 2;

    DX12SwapChain() = default;
    ~DX12SwapChain() = default;

    DX12SwapChain(const DX12SwapChain&) = delete;
    DX12SwapChain& operator=(const DX12SwapChain&) = delete;
    DX12SwapChain(DX12SwapChain&&) = delete;
    DX12SwapChain& operator=(DX12SwapChain&&) = delete;

    bool Init(
        IDXGIFactory6* factory,
        ID3D12CommandQueue* commandQueue,
        ID3D12Device* device,
        void* platformWindowHandle,
        uint32_t width,
        uint32_t height);
    void Shutdown();

    IDXGISwapChain3* GetHandle() const { return m_SwapChain.Get(); }
    UINT GetCurrentBackBufferIndex() const;
    const std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& GetBackBuffers() const
    {
        return m_BackBuffers;
    }
    bool IsValid() const
    {
        return m_SwapChain.Get() != nullptr && m_BackBuffers.size() == BufferCount;
    }

private:
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_BackBuffers;
};

} // namespace Raven
