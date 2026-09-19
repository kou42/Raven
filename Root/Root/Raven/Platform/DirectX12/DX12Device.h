#pragma once

#include "DX12Adapter.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Raven
{

// 選択したDXGI AdapterからID3D12Deviceを生成・所有します。
// CommandQueueやSwapChainはDeviceとは別クラスへ分離し、各初期化段階を独立して確認します。
class DX12Device
{
public:
    DX12Device() = default;
    ~DX12Device() = default;

    DX12Device(const DX12Device&) = delete;
    DX12Device& operator=(const DX12Device&) = delete;
    DX12Device(DX12Device&&) = delete;
    DX12Device& operator=(DX12Device&&) = delete;

    bool Init(const DX12Adapter::AdapterInfo& adapter);
    void Shutdown();
    // Debugビルドでは蓄積されたWarning/Errorを出力します。
    void DrainDebugMessages() const;

    ID3D12Device* GetHandle() const { return m_Device.Get(); }
    bool IsValid() const { return m_Device.Get() != nullptr; }
    D3D_FEATURE_LEVEL GetFeatureLevel() const { return m_FeatureLevel; }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
    D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;
};

} // namespace Raven
