#pragma once

#include "Raven/Renderer/GraphicsContext.h"

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace Raven
{

// Main WindowのContextを借用します。Windowより先に破棄してください。
class OpenGLFrameTimer final
{
public:
    explicit OpenGLFrameTimer(GLFWwindow* pWindow);
    ~OpenGLFrameTimer();
    OpenGLFrameTimer(const OpenGLFrameTimer&) = delete;
    OpenGLFrameTimer& operator=(const OpenGLFrameTimer&) = delete;

    void Begin();
    void End();

private:
    struct Sample
    {
        std::array<unsigned int, 2> Queries{};
        std::uint64_t Frame = 0;
        std::uint64_t Session = 0;
    };

    void Collect();

    GLFWwindow* m_pWindow = nullptr;
    // Render Threadを待たせず、未回収のQuery数を固定上限に抑えます。
    std::array<Sample, 8> m_Samples{};
    Sample* m_pActiveSample = nullptr;
    bool m_HasCheckedSupport = false;
    bool m_IsSupported = false;
};

class OpenGLContext : public GraphicsContext
{
public :

	OpenGLContext();
	OpenGLContext(GLFWwindow* window);

	virtual void Init() override;
	virtual void SwapBuffers() override;

private:
	GLFWwindow* m_WindowHandle;
};

}
