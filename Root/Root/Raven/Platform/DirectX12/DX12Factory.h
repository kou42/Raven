#pragma once

#include <dxgi1_6.h>
#include <wrl/client.h>

namespace Raven
{

// DirectX 12でGPU(Adapter)を列挙する入口となるDXGI Factoryを所有します。
// D3D12 DeviceやCommandQueueとは責務を分離し、VulkanInstanceと同様に
// API初期化の最初の段階を独立して確認できる構造にします。
class DX12Factory
{
public:
    DX12Factory() = default;
    ~DX12Factory() = default;

    DX12Factory(const DX12Factory&) = delete;
    DX12Factory& operator=(const DX12Factory&) = delete;
    DX12Factory(DX12Factory&&) = delete;
    DX12Factory& operator=(DX12Factory&&) = delete;

    bool Init();
    void Shutdown();

    IDXGIFactory6* GetHandle() const { return m_Factory.Get(); }
    bool IsValid() const { return m_Factory.Get() != nullptr; }

private:
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
};

} // namespace Raven
