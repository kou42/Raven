#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"

#include <glad/glad.h>

#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"

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

void OpenGLRHICommandList::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
    if (vertexArray == nullptr)
    {
        return;
    }

    const Ref<IndexBuffer>& indexBuffer = vertexArray->GetIndexBuffer();
    if (indexBuffer == nullptr)
    {
        return;
    }

    const uint32_t resolvedIndexCount = indexCount != 0 ? indexCount : indexBuffer->GetCount();
    if (resolvedIndexCount == 0)
    {
        return;
    }

    // PrimitiveTopologyのRHI化はPipeline移行時に行います。
    // 現段階では既存Rendererの標準描画経路と同じTrianglesを使用し、Command発行責務だけをRHIへ移します。
    vertexArray->Bind();
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(resolvedIndexCount),
        GL_UNSIGNED_INT,
        nullptr);
}

} // namespace Raven
