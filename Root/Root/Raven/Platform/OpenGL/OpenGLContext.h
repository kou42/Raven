#pragma once

#include "../../Renderer/GraphicsContext.h"

struct GLFWwindow;

namespace Raven
{

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