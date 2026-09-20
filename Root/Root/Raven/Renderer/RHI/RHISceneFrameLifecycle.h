#pragma once

#include "Raven/Core/Base.h"
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

    // Frame外でのみ呼び出します。0サイズは最小化中として呼び出し側が保留します。
    // Vulkan/DX12は旧BackBufferへのGPU参照を完了させてから再生成します。
    // RenderTargetに依存するPipeline等の再生成は描画側の責務です。
    virtual bool Resize(uint32_t width, uint32_t height) = 0;

    // Windowの所有権はApplicationに残し、Scene用Frame境界だけを生成します。
    // 未実装BackendをOpenGLへ暗黙に切り替えず、呼び出し元が明示的に失敗を扱います。
    static Scope<RHISceneFrameLifecycle> Create(Window& window);
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

    bool Resize(uint32_t width, uint32_t height) override
    {
        if (m_Window.GetBackend() != RHIBackend::OpenGL ||
            m_FrameActive == true || width == 0 || height == 0)
        {
            return false;
        }

        // OpenGLはSwapChain/Framebufferの再生成を必要としません。
        // Viewportは既存のWindow Resize Event -> Renderer経路が更新します。
        return true;
    }

private:
    Window& m_Window;
    bool m_FrameActive = false;
    bool m_FrameEnded = false;
};

// 現時点で通常Sceneを実行できるのはOpenGLのみです。
// Vulkan / DX12はScene用Acquire・Submit・RenderTargetの実装後にここへ追加します。
inline Scope<RHISceneFrameLifecycle> RHISceneFrameLifecycle::Create(Window& window)
{
    switch (window.GetBackend())
    {
    case RHIBackend::OpenGL:
        return CreateScope<OpenGLSceneFrameLifecycle>(window);
    case RHIBackend::Vulkan:
    case RHIBackend::DirectX12:
    case RHIBackend::DirectX11:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

} // namespace Raven
