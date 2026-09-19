#include "DX12SwapChain.h"

#include <d3d12.h>
#include <Windows.h>

#include <iostream>

namespace Raven
{

bool DX12SwapChain::Init(
    IDXGIFactory6* factory, ID3D12CommandQueue* commandQueue, ID3D12Device* device,
    void* platformWindowHandle, uint32_t width, uint32_t height)
{
    Shutdown();

    if (factory == nullptr || commandQueue == nullptr || device == nullptr ||
        platformWindowHandle == nullptr || width == 0 || height == 0)
    {
        return false;
    }

    HWND windowHandle = static_cast<HWND>(platformWindowHandle);

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width;
    description.Height = height;
    description.Format = m_Format;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = BufferCount;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    HRESULT result = factory->CreateSwapChainForHwnd(
        commandQueue, windowHandle, &description, nullptr, nullptr,
        swapChain.ReleaseAndGetAddressOf());
    if (FAILED(result)) { return false; }

    result = factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result)) { return false; }

    result = swapChain.As(&m_SwapChain);
    if (FAILED(result))
    {
        Shutdown();
        return false;
    }

    if (AcquireBackBuffers() == false)
    {
        Shutdown();
        return false;
    }

    std::cout << "DX12 SwapChain created : " << width << " x " << height << '\n';
    return true;
}

bool DX12SwapChain::Resize(uint32_t width, uint32_t height)
{
    if (m_SwapChain.Get() == nullptr || width == 0 || height == 0)
    {
        return false;
    }

    // ResizeBuffers前に全BackBuffer参照を解放することがDXGIの必須条件です。
    // 呼び出し側はFenceでGPU完了を待ってからこの関数を呼びます。
    m_BackBuffers.clear();

    const HRESULT result = m_SwapChain->ResizeBuffers(
        BufferCount, width, height, m_Format, 0);
    if (FAILED(result))
    {
        std::cout << "Failed to resize DX12 SwapChain. HRESULT = 0x"
                  << std::hex << static_cast<unsigned long>(result) << std::dec << '\n';
        return false;
    }

    return AcquireBackBuffers();
}

bool DX12SwapChain::AcquireBackBuffers()
{
    m_BackBuffers.resize(BufferCount);
    for (UINT index = 0; index < BufferCount; ++index)
    {
        const HRESULT result = m_SwapChain->GetBuffer(
            index, IID_PPV_ARGS(m_BackBuffers[index].ReleaseAndGetAddressOf()));
        if (FAILED(result))
        {
            m_BackBuffers.clear();
            return false;
        }
    }
    return true;
}

void DX12SwapChain::Shutdown()
{
    m_BackBuffers.clear();
    m_SwapChain.Reset();
}

UINT DX12SwapChain::GetCurrentBackBufferIndex() const
{
    if (m_SwapChain.Get() == nullptr) { return 0; }
    return m_SwapChain->GetCurrentBackBufferIndex();
}

} // namespace Raven
