#include "DX12Device.h"

#include <iostream>
#include <vector>

namespace Raven
{

bool DX12Device::Init(const DX12Adapter::AdapterInfo& adapter)
{
    if (m_Device.Get() != nullptr)
    {
        return true;
    }

    if (adapter.Handle.Get() == nullptr)
    {
        std::cout << "Cannot create DX12 Device because Adapter is null.\n";
        return false;
    }

    // 高いFeature Levelから順に試し、GPUが実際に対応する最大レベルでDeviceを作ります。
    // 最初からFeature Levelを固定しないことで、異なる世代のGPUでも初期化経路を確認できます。
    constexpr D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    for (const D3D_FEATURE_LEVEL featureLevel : featureLevels)
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        const HRESULT result = D3D12CreateDevice(
            adapter.Handle.Get(),
            featureLevel,
            IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));

        if (SUCCEEDED(result))
        {
            m_Device = device;
            m_FeatureLevel = featureLevel;

            std::cout << "DX12 ID3D12Device created successfully.\n";
            std::cout << "  Feature Level : 0x"
                      << std::hex << static_cast<unsigned int>(m_FeatureLevel)
                      << std::dec << '\n';
            return true;
        }
    }

    std::cout << "Failed to create DX12 ID3D12Device with the selected Adapter.\n";
    return false;
}

void DX12Device::DrainDebugMessages() const
{
#if defined(_DEBUG)
    if (m_Device.Get() == nullptr)
    {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(m_Device.As(&infoQueue)))
    {
        return;
    }

    const UINT64 messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 index = 0; index < messageCount; ++index)
    {
        SIZE_T messageSize = 0;
        if (FAILED(infoQueue->GetMessage(index, nullptr, &messageSize)) || messageSize == 0)
        {
            continue;
        }

        std::vector<unsigned char> storage(messageSize);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(infoQueue->GetMessage(index, message, &messageSize)))
        {
            continue;
        }

        // 通常のINFO/MESSAGEは大量に発生するため、調査対象の警告とエラーに絞ります。
        if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING ||
            message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
        {
            std::cerr << "[DX12 InfoQueue][" << static_cast<int>(message->Severity)
                      << "][ID " << static_cast<int>(message->ID) << "] "
                      << (message->pDescription != nullptr ? message->pDescription : "")
                      << '\n';
        }
    }
    // 同じメッセージをフレームごとに繰り返し出さないよう、取得後に消去します。
    infoQueue->ClearStoredMessages();
#endif
}

void DX12Device::Shutdown()
{
    DrainDebugMessages();
#if defined(_DEBUG)
    if (m_Device.Get() != nullptr)
    {
        // Queue/SwapChainなどを解放した後に呼ぶことで、残存Device子オブジェクトを検出します。
        // ReportLiveDeviceObjectsはDevice自体の参照カウントを調べるAPIではありません。
        Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
        if (SUCCEEDED(m_Device.As(&debugDevice)))
        {
            debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        }
    }
#endif
    m_Device.Reset();
    m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;
}

} // namespace Raven
