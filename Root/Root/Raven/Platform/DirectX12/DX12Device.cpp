#include "DX12Device.h"

#include <iostream>

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

void DX12Device::Shutdown()
{
    m_Device.Reset();
    m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;
}

} // namespace Raven
