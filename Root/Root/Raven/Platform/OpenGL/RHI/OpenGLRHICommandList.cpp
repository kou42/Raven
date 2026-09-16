#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"

#include <glad/glad.h>

namespace Raven
{

void OpenGLRHICommandList::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    glViewport(
        static_cast<GLint>(x),
        static_cast<GLint>(y),
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height));
}

void OpenGLRHICommandList::SetClearColor(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
}

void OpenGLRHICommandList::Clear()
{
    // 直前のTransparent pass等でDepth Writeが無効化されている可能性があります。
    // Depth Bufferを確実にclearするため、既存OpenGLRendererAPIと同じく明示的に有効化します。
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

} // namespace Raven
