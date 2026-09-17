#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace Raven
{

// Graphics CommandをGPUへ投入するDirect Queueを所有します。
// Fence同期やCommandList実行は後続ステップで追加し、ここではQueue生成だけを担当します。
class DX12CommandQueue
{
public:
    DX12CommandQueue() = default;
    ~DX12CommandQueue() = default;

    DX12CommandQueue(const DX12CommandQueue&) = delete;
    DX12CommandQueue& operator=(const DX12CommandQueue&) = delete;
    DX12CommandQueue(DX12CommandQueue&&) = delete;
    DX12CommandQueue& operator=(DX12CommandQueue&&) = delete;

    bool Init(ID3D12Device* device);
    void Shutdown();

    ID3D12CommandQueue* GetHandle() const { return m_CommandQueue.Get(); }
    bool IsValid() const { return m_CommandQueue.Get() != nullptr; }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
};

} // namespace Raven
