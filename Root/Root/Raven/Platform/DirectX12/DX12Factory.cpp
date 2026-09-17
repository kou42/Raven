#include "DX12Factory.h"

#include <iostream>

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
    // DXGI Debug flagはデバッグビルドでのみ有効化します。
    // DXGI側の診断情報を得やすくしつつ、Releaseビルドへ不要なDebug依存を持ち込みません。
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    const HRESULT result = CreateDXGIFactory2(
        factoryFlags,
        IID_PPV_ARGS(m_Factory.ReleaseAndGetAddressOf()));

    if (FAILED(result))
    {
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
