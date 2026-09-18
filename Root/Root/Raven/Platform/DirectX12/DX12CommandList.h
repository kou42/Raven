#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace Raven
{

// Direct CommandAllocatorとGraphicsCommandListを一組で所有します。
// Fence同期が入るまではGPU実行中のAllocatorをResetしないことを呼び出し側の前提とします。
class DX12CommandList
{
public:
    DX12CommandList() = default;
    ~DX12CommandList() = default;

    DX12CommandList(const DX12CommandList&) = delete;
    DX12CommandList& operator=(const DX12CommandList&) = delete;
    DX12CommandList(DX12CommandList&&) = delete;
    DX12CommandList& operator=(DX12CommandList&&) = delete;

    bool Init(ID3D12Device* device);
    bool Reset(ID3D12PipelineState* initialPipelineState = nullptr);
    bool Close();
    void Shutdown();

    ID3D12CommandAllocator* GetAllocator() const { return m_CommandAllocator.Get(); }
    ID3D12GraphicsCommandList* GetHandle() const { return m_CommandList.Get(); }
    bool IsValid() const
    {
        return m_CommandAllocator.Get() != nullptr &&
               m_CommandList.Get() != nullptr;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
};

} // namespace Raven
