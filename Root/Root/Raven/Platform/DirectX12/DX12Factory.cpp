#include "DX12Factory.h"

#include <iostream>
#include <d3d12.h>

namespace Raven
{

bool DX12Factory::Init()
{
    if (m_Factory.Get() != nullptr)
    {
        // 二重初期化時に既存Factoryを失わないよう、生成済みならそのまま成功とします。
        return true;
    }

    UINT factoryFlags = 0;

#if defined(_DEBUG)
    // Device生成前に有効化する必要があります。Graphics Tools未導入時は診断を諦めて続行します。
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    const HRESULT debugResult = D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()));
    if (SUCCEEDED(debugResult))
    {
        debugController->EnableDebugLayer();
        std::cout << "[DX12 Debug] D3D12 Debug Layer enabled.\\n";
    }
    else
    {
        std::cerr << "[DX12 Debug] D3D12 Debug Layer unavailable; continuing without it.\\n";
    }
#endif

#if defined(_DEBUG)
    // DXGI Debug flagはデバッグビルドでのみ有効化します。
    // DXGI側の診断情報を得やすくしつつ、Releaseビルドへ不要なDebug依存を持ち込みません。
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    const HRESULT result = CreateDXGIFactory2(
        factoryFlags,
        IID_PPV_ARGS(m_Factory.ReleaseAndGetAddressOf()));

    if (FAILED(result))
    {
#if defined(_DEBUG)
        // DXGI debug runtimeがない場合はDebug flagなしでFactoryを再試行します。
        if (factoryFlags != 0)
        {
            std::cerr << "[DX12 Debug] DXGI debug factory unavailable; retrying without debug flag.\\n";
            const HRESULT fallbackResult = CreateDXGIFactory2(
                0, IID_PPV_ARGS(m_Factory.ReleaseAndGetAddressOf()));
            if (SUCCEEDED(fallbackResult))
            {
                return true;
            }
        }
#endif
        m_Factory.Reset();
        std::cout << "Failed to create DXGI Factory. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    std::cout << "DXGI Factory created successfully.\n";
    return true;
}

void DX12Factory::Shutdown()
{
    // ComPtr::ResetでReleaseを一箇所に集約し、明示Shutdownとデストラクタの双方で
    // COMオブジェクトのLifetimeを安全に管理できるようにします。
    m_Factory.Reset();
}

} // namespace Raven
