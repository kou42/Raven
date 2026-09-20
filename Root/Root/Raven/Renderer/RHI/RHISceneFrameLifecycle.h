#pragma once

#include "Raven/Core/Window.h"
#include "Raven/Renderer/RHI/RHIClearContext.h"

namespace Raven
{

// Scene / Layer / ImGui / Raven UIを含むApplication Frameの境界です。
// Clear DemoのRHIFrameLifecycleと異なり、ClearFrameを必須としません。
// Scene描画のClearはRenderCommand / RHICommandList側の責務として維持します。
class RHISceneFrameLifecycle
{
public:
    virtual ~RHISceneFrameLifecycle() = default;

    virtual RHIFrameResult BeginFrame() = 0;
    virtual RHIFrameResult EndFrame() = 0;
    virtual RHIFrameResult Present() = 0;
};

// 現行Scene描画はOpenGLのImmediate CommandListを使用します。
// GPU CommandBufferのAcquire / Submitを持たないため、Begin/Endは状態境界のみです。
// WindowはApplicationが所有し、このAdapterは借用します。
class OpenGLSceneFrameLifecycle final : public RHISceneFrameLifecycle
{
public:
    explicit OpenGLSceneFrameLifecycle(Window& window)
        : m_Window(window)
    {
    }

    RHIFrameResult BeginFrame() override
    {
        if (m_Window.GetBackend() != RHIBackend::OpenGL || m_FrameActive == true)
        {
            return RHIFrameResult::FatalError;
        }

        m_FrameActive = true;
        m_FrameEnded = false;
        return RHIFrameResult::Success;
    }

    RHIFrameResult EndFrame() override
    {
        if (m_FrameActive == false || m_FrameEnded == true)
        {
            return RHIFrameResult::FatalError;
        }

        // OpenGLの描画命令は発行済み。ここではSwapBuffersを実行しません。
        m_FrameEnded = true;
        return RHIFrameResult::Success;
    }

    RHIFrameResult Present() override
    {
        if (m_FrameActive == false || m_FrameEnded == false)
        {
            return RHIFrameResult::FatalError;
        }

        // Presentは1 Frameに1回だけ。Window::OnUpdate()を併用しません。
        m_Window.Present();
        m_FrameActive = false;
        m_FrameEnded = false;
        return RHIFrameResult::Success;
    }

private:
    Window& m_Window;
    bool m_FrameActive = false;
    bool m_FrameEnded = false;
};

} // namespace Raven
