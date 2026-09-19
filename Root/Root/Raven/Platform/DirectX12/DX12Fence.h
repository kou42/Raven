#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace Raven
{

// CommandQueueの完了値をCPUから待機するFenceを所有します。
// CommandAllocatorを安全にResetするための最小同期機構です。
class DX12Fence
{
public:
    DX12Fence() = default;
    ~DX12Fence();

    DX12Fence(const DX12Fence&) = delete;
    DX12Fence& operator=(const DX12Fence&) = delete;
    DX12Fence(DX12Fence&&) = delete;
    DX12Fence& operator=(DX12Fence&&) = delete;

    bool Init(ID3D12Device* device);
    bool SignalAndWait(ID3D12CommandQueue* commandQueue);
    void Shutdown();

    ID3D12Fence* GetHandle() const { return m_Fence.Get(); }
    bool IsValid() const { return m_Fence.Get() != nullptr && m_EventHandle != nullptr; }

private:
    Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
    void* m_EventHandle = nullptr;
    uint64_t m_NextFenceValue = 1;
};

} // namespace Raven
