#include "DX12CommandQueue.h"

#include <iostream>

namespace Raven
{

bool DX12CommandQueue::Init(ID3D12Device* device)
{
    if (m_CommandQueue.Get() != nullptr)
    {
        return true;
    }

    if (device == nullptr)
    {
        std::cout << "Cannot create DX12 CommandQueue because Device is null.\n";
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC description{};
    description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    description.NodeMask = 0;

    // Triangle描画までのGraphics処理を一つのDirect Queueへ集約します。
    // Copy/Compute専用Queueは必要性が明確になった段階で追加し、初期実装を複雑化しません。
    const HRESULT result = device->CreateCommandQueue(
        &description,
        IID_PPV_ARGS(m_CommandQueue.ReleaseAndGetAddressOf()));

    if (FAILED(result))
    {
        m_CommandQueue.Reset();
        std::cout << "Failed to create DX12 Graphics CommandQueue. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    std::cout << "DX12 Graphics CommandQueue created successfully.\n";
    return true;
}

void DX12CommandQueue::Shutdown()
{
    m_CommandQueue.Reset();
}

} // namespace Raven
