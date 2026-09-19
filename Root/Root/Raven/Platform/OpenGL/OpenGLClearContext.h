#pragma once

#include "Raven/Renderer/RHI/RHIClearContext.h"
#include "Raven/Renderer/RHI/RHIFrameLifecycle.h"

struct GLFWwindow;

namespace Raven
{
// 既存OpenGL Applicationとは独立したClear検証経路です。
// OpenGL Context自体はWindowsWindowが所有するため、ここでは借用だけします。
class OpenGLClearContext : public RHIClearContext, public RHIFrameLifecycle
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

private:
    GLFWwindow* m_Window = nullptr;
    bool m_FrameActive = false;
    bool m_FrameEnded = false;
};
} // namespace Raven
