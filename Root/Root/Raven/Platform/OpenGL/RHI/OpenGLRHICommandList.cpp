#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"

#include <glad/glad.h>

#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"

namespace Raven
{
namespace
{

GLenum ToOpenGLPrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::Triangles:
        return GL_TRIANGLES;
    case PrimitiveTopology::Lines:
        return GL_LINES;
    case PrimitiveTopology::Points:
        return GL_POINTS;
    case PrimitiveTopology::None:
    default:
        return GL_NONE;
    }
}

} // namespace

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

void OpenGLRHICommandList::BindPipeline(const Ref<Pipeline>& pipeline)
{
    if (pipeline == nullptr)
    {
        return;
    }

    // 現段階ではLegacy PipelineがOpenGL stateとShader bindingを適用します。
    // CommandList側で現在Pipelineを保持することで、DrawIndexedはRendererAPIへ戻らず
    // PrimitiveTopologyを正しく解決できます。RHIPipeline導入後はこの依存を置き換えます。
    pipeline->Bind();
    m_CurrentPipeline = pipeline;
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

    uint32_t resolvedIndexCount = indexCount;
    if (resolvedIndexCount == 0)
    {
        resolvedIndexCount = indexBuffer->GetCount();
    }

    if (resolvedIndexCount == 0)
    {
        return;
    }

    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    if (m_CurrentPipeline != nullptr)
    {
        topology = m_CurrentPipeline->GetSpecification().Topology;
    }

    const GLenum primitive = ToOpenGLPrimitiveTopology(topology);
    if (primitive == GL_NONE)
    {
        return;
    }

    vertexArray->Bind();
    glDrawElements(
        primitive,
        static_cast<GLsizei>(resolvedIndexCount),
        GL_UNSIGNED_INT,
        nullptr);
}

} // namespace Raven
