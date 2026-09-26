#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Core/Window.h"
#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHIBuffer.h"
#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"
#include "Raven/Renderer/RHI/RHIMaterialProperties.h"

#include <array>

namespace Raven
{

// API非依存のExplicit Scene描画命令です。Frameの開始・終了はLifecycleが管理し、
// CommandListはそのFrame内でだけ描画命令を発行します。
// Legacy RHICommandListのOpenGL VertexArray/Shader APIとは独立させます。
class RHISceneCommandList
{
public:
    virtual ~RHISceneCommandList() = default;

    virtual bool SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual bool SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual bool ClearColor(const float color[4]) = 0;
    virtual bool BindPipeline(const Ref<RHIGraphicsPipeline>& pipeline) = 0;
    virtual bool BindMaterial(const RHIMaterialProperties& material) = 0;
    // column-majorのclip-space変換行列を設定します。
    virtual bool SetClipTransform(const std::array<float, 16>& transform) = 0;
    virtual bool DrawIndexed(const Ref<RHIBuffer>& vertexBuffer,
        const Ref<RHIBuffer>& indexBuffer, uint32_t indexCount = 0,
        uint32_t firstIndex = 0) = 0;
};

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

        // 各WindowのContextをFrame開始時にCurrentにします。
        // RendererのGPUリソース共有は別途扱い、ここでは描画先の切替だけを保証します。
        if (m_Window.MakeContextCurrent() == false)
        {
            return RHIFrameResult::FatalError;
        }
        // ContextごとにViewport状態は独立するため、切替直後に実Pixelサイズを設定します。
        // 最小化中は0サイズを渡さずFrame開始を保留します。
        if (m_Window.SetFramebufferViewport() == false)
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
