#include "DX12SwapChain.h"

#include <d3d12.h>
#include <Windows.h>

#include <iostream>

namespace Raven
{

bool DX12SwapChain::Init(
    IDXGIFactory6* factory,
    ID3D12CommandQueue* commandQueue,
    ID3D12Device* device,
    void* platformWindowHandle,
    uint32_t width,
    uint32_t height)
{
    Shutdown();

    if (factory == nullptr || commandQueue == nullptr || device == nullptr)
    {
        std::cout << "Cannot create DX12 SwapChain because Factory, Queue, or Device is null.\n";
        return false;
    }

    if (platformWindowHandle == nullptr)
    {
        std::cout << "Cannot create DX12 SwapChain because HWND is null.\n";
        return false;
    }

    HWND windowHandle = static_cast<HWND>(platformWindowHandle);

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width;
    description.Height = height;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = BufferCount;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    description.Flags = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    HRESULT result = factory->CreateSwapChainForHwnd(
        commandQueue,
        windowHandle,
        &description,
        nullptr,
        nullptr,
        swapChain.ReleaseAndGetAddressOf());
    if (FAILED(result))
    {
        std::cout << "Failed to create DX12 SwapChain. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    // Alt+EnterによるDXGIの自動Fullscreen切替を無効化し、
    // Window/SwapChainの状態遷移をRaven側で明示的に管理できるようにします。
    result = factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result))
    {
        std::cout << "Failed to configure DXGI Window association. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        return false;
    }

    result = swapChain.As(&m_SwapChain);
    if (FAILED(result))
    {
        m_SwapChain.Reset();
        return false;
    }

    m_BackBuffers.resize(BufferCount);
    for (UINT bufferIndex = 0; bufferIndex < BufferCount; ++bufferIndex)
    {
        result = m_SwapChain->GetBuffer(
            bufferIndex,
            IID_PPV_ARGS(m_BackBuffers[bufferIndex].ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            std::cout << "Failed to get DX12 SwapChain BackBuffer. HRESULT = 0x"
                      << std::hex << static_cast<unsigned long>(result)
                      << std::dec << '\n';
            Shutdown();
            return false;
        }
    }

    std::cout << "DX12 SwapChain created successfully.\n";
    std::cout << "  BackBuffers : " << m_BackBuffers.size() << '\n';
    std::cout << "  Size : " << width << " x " << height << '\n';
    return true;
}

void DX12SwapChain::Shutdown()
{
    m_BackBuffers.clear();
    m_SwapChain.Reset();
}

UINT DX12SwapChain::GetCurrentBackBufferIndex() const
{
    if (m_SwapChain.Get() == nullptr)
    {
        return 0;
    }

    return m_SwapChain->GetCurrentBackBufferIndex();
}

} // namespace Raven
