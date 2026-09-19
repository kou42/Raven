#include "DX12Adapter.h"

#include <iostream>

namespace Raven
{

bool DX12Adapter::Enumerate(IDXGIFactory6* factory)
{
    Clear();

    if (factory == nullptr)
    {
        std::cout << "Cannot enumerate DX12 adapters because DXGI Factory is null.\n";
        return false;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT result = factory->EnumAdapterByGpuPreference(
            adapterIndex,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));

        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        if (FAILED(result))
        {
            std::cout << "Failed to enumerate DX12 adapter. HRESULT = 0x"
                      << std::hex << static_cast<unsigned long>(result)
                      << std::dec << '\n';
            Clear();
            return false;
        }

        DXGI_ADAPTER_DESC1 description{};
        const HRESULT descResult = adapter->GetDesc1(&description);
        if (FAILED(descResult))
        {
            std::cout << "Failed to get DX12 adapter description. HRESULT = 0x"
                      << std::hex << static_cast<unsigned long>(descResult)
                      << std::dec << '\n';
            continue;
        }

        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            // WARP等のSoftware AdapterはHardware GPU一覧から除外します。
            // 必要になった場合はFallback Backendとして別経路で明示的に選択します。
            continue;
        }

        AdapterInfo info{};
        info.Handle = adapter;
        info.Description = description;
        m_Adapters.push_back(info);

        std::wcout << L"DX12 Adapter [" << (m_Adapters.size() - 1) << L"]\n";
        std::wcout << L"  Name : " << description.Description << L'\n';
        std::wcout << L"  Vendor ID : " << description.VendorId << L'\n';
        std::wcout << L"  Device ID : " << description.DeviceId << L'\n';
        std::wcout << L"  Dedicated Video Memory : "
                   << static_cast<unsigned long long>(description.DedicatedVideoMemory)
                   << L" bytes\n";
    }

    if (m_Adapters.empty())
    {
        std::cout << "No hardware DX12 adapter was found.\n";
        return false;
    }

    return true;
}

void DX12Adapter::Clear()
{
    m_Adapters.clear();
}

} // namespace Raven
