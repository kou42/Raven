#include "DX12Fence.h"

#include <Windows.h>

#include <iostream>

namespace Raven
{

DX12Fence::~DX12Fence()
{
    Shutdown();
}

bool DX12Fence::Init(ID3D12Device* device)
{
    Shutdown();

    if (device == nullptr)
    {
        return false;
    }

    HRESULT result = device->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(m_Fence.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        return false;
    }

    HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr)
    {
        Shutdown();
        return false;
    }

    m_EventHandle = eventHandle;
    m_NextFenceValue = 1;
    std::cout << "DX12 Fence/Event created successfully.\n";
    return true;
}

bool DX12Fence::Signal(ID3D12CommandQueue* commandQueue, uint64_t& signaledValue)
{
    if (IsValid() == false || commandQueue == nullptr)
    {
        return false;
    }

    const uint64_t fenceValue = m_NextFenceValue;
    const HRESULT result = commandQueue->Signal(m_Fence.Get(), fenceValue);
    if (FAILED(result))
    {
        return false;
    }
    // Signal成功時だけ番号を進め、失敗時に未投入のFence値を待たないようにします。
    m_NextFenceValue = fenceValue + 1;
    signaledValue = fenceValue;
    return true;
}

bool DX12Fence::Wait(uint64_t fenceValue)
{
    if (IsValid() == false)
    {
        return false;
    }
    if (fenceValue == 0)
    {
        return true;
    }

    const uint64_t completed = m_Fence->GetCompletedValue();
    if (completed == UINT64_MAX)
    {
        // Device Removed時はFence待機が完了しない可能性があります。
        return false;
    }
    if (completed >= fenceValue)
    {
        return true;
    }

    if (FAILED(m_Fence->SetEventOnCompletion(
        fenceValue, static_cast<HANDLE>(m_EventHandle))))
    {
        return false;
    }
    return WaitForSingleObject(static_cast<HANDLE>(m_EventHandle), INFINITE) == WAIT_OBJECT_0 &&
        m_Fence->GetCompletedValue() != UINT64_MAX;
}

bool DX12Fence::SignalAndWait(ID3D12CommandQueue* commandQueue)
{
    uint64_t value = 0;
    if (Signal(commandQueue, value) == false)
    {
        return false;
    }
    return Wait(value);
}

void DX12Fence::Shutdown()
{
    if (m_EventHandle != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(m_EventHandle));
        m_EventHandle = nullptr;
    }

    m_Fence.Reset();
    m_NextFenceValue = 1;
}

} // namespace Raven
