#pragma once

#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHIFrameLifecycle.h"

#include "DX12Adapter.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12Device.h"
#include "DX12Factory.h"
#include "DX12Fence.h"
#include "DX12FrameRenderer.h"
#include "DX12SwapChain.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
class Window;

// DX12初期化とClear表示を束ねる学習用Context。既存OpenGL Rendererとは独立です。
class DX12ClearContext : public RHIClearContext, public RHIFrameLifecycle
{
public:
    bool Init(Window& window) override;
    RHIFrameResult DrawClearFrame(const float clearColor[4]) override;
    RHIFrameResult BeginFrame() override;
    RHIFrameResult ClearFrame(const float clearColor[4]) override;
    RHIFrameResult EndFrame() override;
    RHIFrameResult Present() override;
    bool Resize(uint32_t width, uint32_t height) override;
    void Shutdown() override;
    ~DX12ClearContext();

private:
    DX12Factory m_Factory;
    DX12Adapter m_Adapters;
    DX12Device m_Device;
    DX12CommandQueue m_Queue;
    DX12SwapChain m_SwapChain;
    struct FrameResource
    {
        std::unique_ptr<DX12CommandList> CommandList;
        uint64_t FenceValue = 0;
    };
    std::vector<FrameResource> m_Frames;
    uint32_t m_CurrentFrame = 0;
    DX12Fence m_Fence;
    DX12FrameRenderer m_FrameRenderer;
    bool m_VSync = true;
    bool m_FrameActive = false;
    bool m_FrameSubmitted = false;
};
} // namespace Raven
