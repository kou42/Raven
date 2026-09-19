#include "DX12CommandList.h"

#include <iostream>

namespace Raven
{

bool DX12CommandList::Init(ID3D12Device* device)
{
    Shutdown();

    if (device == nullptr)
    {
        std::cout << "Cannot create DX12 CommandList because Device is null.\n";
        return false;
    }

    HRESULT result = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(m_CommandAllocator.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        std::cout << "Failed to create DX12 CommandAllocator. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    result = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_CommandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(m_CommandList.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        std::cout << "Failed to create DX12 GraphicsCommandList. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        Shutdown();
        return false;
    }

    // CreateCommandList直後はRecording状態です。
    // 通常Frame処理をReset -> Record -> Closeで統一するため、初期化時はいったんCloseします。
    result = m_CommandList->Close();
    if (FAILED(result))
    {
        std::cout << "Failed to close initial DX12 GraphicsCommandList. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        Shutdown();
        return false;
    }

    std::cout << "DX12 CommandAllocator and GraphicsCommandList created successfully.\n";
    return true;
}

bool DX12CommandList::Reset(ID3D12PipelineState* initialPipelineState)
{
    if (IsValid() == false)
    {
        return false;
    }

    // AllocatorはGPUが参照し終わってからResetする必要があります。
    // その保証は次段階のFenceで行い、このクラスではD3D12の正しいReset順だけを保持します。
    HRESULT result = m_CommandAllocator->Reset();
    if (FAILED(result))
    {
        std::cout << "Failed to reset DX12 CommandAllocator. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    result = m_CommandList->Reset(
        m_CommandAllocator.Get(),
        initialPipelineState);
    if (FAILED(result))
    {
        std::cout << "Failed to reset DX12 GraphicsCommandList. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    return true;
}

bool DX12CommandList::Close()
{
    if (IsValid() == false)
    {
        return false;
    }

    const HRESULT result = m_CommandList->Close();
    if (FAILED(result))
    {
        std::cout << "Failed to close DX12 GraphicsCommandList. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    return true;
}

void DX12CommandList::Shutdown()
{
    m_CommandList.Reset();
    m_CommandAllocator.Reset();
}

} // namespace Raven
