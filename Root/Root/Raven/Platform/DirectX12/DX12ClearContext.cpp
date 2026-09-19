#include "DX12ClearContext.h"

#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

namespace Raven
{
DX12ClearContext::~DX12ClearContext()
{
    Shutdown();
}

bool DX12ClearContext::Init(Window& window)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::DirectX12 ||
        window.GetPlatformWindowHandle() == nullptr ||
        window.GetNativeWindow() == nullptr)
    {
        return false;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(window.GetNativeWindow()),
        &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return false;
    }

    if (m_Factory.Init() == false || m_Adapters.Enumerate(m_Factory.GetHandle()) == false)
    {
        Shutdown();
        return false;
    }

    // Adapterの列挙順はD3D12 Device生成可能性を保証しないため、候補を順に試します。
    bool deviceCreated = false;
    for (const auto& adapter : m_Adapters.GetAdapters())
    {
        if (m_Device.Init(adapter) == true)
        {
            deviceCreated = true;
            break;
        }
    }
    if (deviceCreated == false ||
        m_Queue.Init(m_Device.GetHandle()) == false ||
        m_SwapChain.Init(m_Factory.GetHandle(), m_Queue.GetHandle(),
            m_Device.GetHandle(), window.GetPlatformWindowHandle(),
            static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight)) == false ||
        m_Fence.Init(m_Device.GetHandle()) == false ||
        m_FrameRenderer.Init(m_Device.GetHandle(), m_SwapChain) == false)
    {
        Shutdown();
        return false;
    }

    // SwapChainのBufferCountに合わせてFrame別Allocatorを確保します。
    for (uint32_t index = 0; index < DX12SwapChain::BufferCount; ++index)
    {
        FrameResource frame;
        frame.CommandList = std::make_unique<DX12CommandList>();
        if (frame.CommandList->Init(m_Device.GetHandle()) == false)
        {
            Shutdown();
            return false;
        }
        m_Frames.push_back(std::move(frame));
    }
    m_Device.DrainDebugMessages();
    m_CurrentFrame = 0;
    m_VSync = window.IsVSync();
    return true;
}

RHIFrameResult DX12ClearContext::DrawClearFrame(const float clearColor[4])
{
    RHIFrameResult result = BeginFrame();
    if (result != RHIFrameResult::Success)
    {
        return result;
    }
    result = ClearFrame(clearColor);
    if (result != RHIFrameResult::Success)
    {
        return result;
    }
    result = EndFrame();
    if (result != RHIFrameResult::Success)
    {
        return result;
    }
    return Present();
}

RHIFrameResult DX12ClearContext::BeginFrame()
{
    if (m_FrameActive == true || m_CurrentFrame >= m_Frames.size() ||
        m_Frames[m_CurrentFrame].CommandList == nullptr)
    {
        return RHIFrameResult::FatalError;
    }

    FrameResource& frame = m_Frames[m_CurrentFrame];
    if (m_FrameRenderer.BeginFrame(m_SwapChain, *frame.CommandList,
        m_Fence, frame.FenceValue) == false)
    {
        m_Device.DrainDebugMessages();
        return RHIFrameResult::FatalError;
    }
    m_FrameActive = true;
    m_FrameSubmitted = false;
    return RHIFrameResult::Success;
}

RHIFrameResult DX12ClearContext::ClearFrame(const float clearColor[4])
{
    if (m_FrameActive == false || clearColor == nullptr)
    {
        return RHIFrameResult::FatalError;
    }
    if (m_FrameRenderer.ClearFrame(m_SwapChain,
        *m_Frames[m_CurrentFrame].CommandList, clearColor) == false)
    {
        m_Device.DrainDebugMessages();
        return RHIFrameResult::FatalError;
    }
    return RHIFrameResult::Success;
}

RHIFrameResult DX12ClearContext::EndFrame()
{
    if (m_FrameActive == false || m_FrameSubmitted == true)
    {
        return RHIFrameResult::FatalError;
    }
    if (m_FrameRenderer.EndFrame(m_Queue,
        *m_Frames[m_CurrentFrame].CommandList) == false)
    {
        m_Device.DrainDebugMessages();
        return RHIFrameResult::FatalError;
    }
    m_FrameSubmitted = true;
    return RHIFrameResult::Success;
}

RHIFrameResult DX12ClearContext::Present()
{
    if (m_FrameActive == false || m_FrameSubmitted == false)
    {
        return RHIFrameResult::FatalError;
    }

    FrameResource& frame = m_Frames[m_CurrentFrame];
    const bool presented = m_FrameRenderer.Present(
        m_SwapChain, m_Queue, m_Fence, frame.FenceValue, m_VSync);
    m_Device.DrainDebugMessages();
    if (presented == false)
    {
        // Submit後の失敗はFatalErrorとして扱い、Contextを再利用しません。
        return RHIFrameResult::FatalError;
    }
    m_FrameActive = false;
    m_FrameSubmitted = false;
    m_CurrentFrame = (m_CurrentFrame + 1) % static_cast<uint32_t>(m_Frames.size());
    return RHIFrameResult::Success;
}

bool DX12ClearContext::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || m_FrameActive == true ||
        m_SwapChain.IsValid() == false || m_Queue.IsValid() == false)
    {
        return false;
    }

    // 旧BackBufferへのGPU参照を終わらせてからResizeBuffersします。
    if (m_Fence.SignalAndWait(m_Queue.GetHandle()) == false ||
        m_SwapChain.Resize(width, height) == false)
    {
        m_Device.DrainDebugMessages();
        return false;
    }
    const bool rebuilt = m_FrameRenderer.RebuildRenderTargets(m_Device.GetHandle(), m_SwapChain);
    m_Device.DrainDebugMessages();
    return rebuilt;
}

void DX12ClearContext::Shutdown()
{
    if (m_Fence.IsValid() == true && m_Queue.IsValid() == true)
    {
        m_Fence.SignalAndWait(m_Queue.GetHandle());
    }
    m_FrameRenderer.Shutdown();
    m_Fence.Shutdown();
    m_Frames.clear();
    m_CurrentFrame = 0;
    m_FrameActive = false;
    m_FrameSubmitted = false;
    m_SwapChain.Shutdown();
    m_Queue.Shutdown();
    m_Device.Shutdown();
    m_Adapters.Clear();
    m_Factory.Shutdown();
}
} // namespace Raven
