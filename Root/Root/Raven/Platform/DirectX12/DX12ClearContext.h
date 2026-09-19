#pragma once

#include "DX12Adapter.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12Device.h"
#include "DX12Factory.h"
#include "DX12Fence.h"
#include "DX12FrameRenderer.h"
#include "DX12SwapChain.h"

#include <cstdint>

namespace Raven
{
class Window;

// DX12初期化とClear表示を束ねる学習用Context。既存OpenGL Rendererとは独立です。
class DX12ClearContext
{
public:
    bool Init(Window& window);
    bool DrawClearFrame(const float clearColor[4]);
    bool Resize(uint32_t width, uint32_t height);
    void Shutdown();
    ~DX12ClearContext();

private:
    DX12Factory m_Factory;
    DX12Adapter m_Adapters;
    DX12Device m_Device;
    DX12CommandQueue m_Queue;
    DX12SwapChain m_SwapChain;
    DX12CommandList m_CommandList;
    DX12Fence m_Fence;
    DX12FrameRenderer m_FrameRenderer;
    bool m_VSync = true;
};
} // namespace Raven
