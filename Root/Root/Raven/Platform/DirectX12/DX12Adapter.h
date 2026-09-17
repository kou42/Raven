#pragma once

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vector>

namespace Raven
{

// DXGI Factoryから利用可能なHardware Adapter(GPU)を列挙します。
// Software Adapterは描画用GPU候補から除外し、D3D12 Device生成可能性の確認は
// 次のDevice生成ステップで行えるようAdapter情報を保持します。
class DX12Adapter
{
public:
    struct AdapterInfo
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> Handle;
        DXGI_ADAPTER_DESC1 Description{};
    };

    bool Enumerate(IDXGIFactory6* factory);
    void Clear();

    const std::vector<AdapterInfo>& GetAdapters() const { return m_Adapters; }
    bool HasAdapters() const { return m_Adapters.empty() == false; }

private:
    std::vector<AdapterInfo> m_Adapters;
};

} // namespace Raven
