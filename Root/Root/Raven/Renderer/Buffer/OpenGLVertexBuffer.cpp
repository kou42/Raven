#include "Raven/Renderer/Buffer/OpenGLVertexBuffer.h"

#include <glad/glad.h>

#include "Raven/Platform/OpenGL/RHI/OpenGLRHIBuffer.h"

namespace Raven
{

OpenGLVertexBuffer::OpenGLVertexBuffer(const float* vertices, uint32_t size)
{
    RecreateBuffer(vertices, size);
}

void OpenGLVertexBuffer::Bind() const
{
    if (m_RHIBuffer == nullptr)
    {
        return;
    }

    // VAOの頂点属性定義は現在もGL_ARRAY_BUFFER bindingを参照するため、
    // CommandList移行までの互換処理としてnative handleをBackend内部でbindします。
    glBindBuffer(GL_ARRAY_BUFFER, m_RHIBuffer->GetRendererID());
}

void OpenGLVertexBuffer::Unbind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
{
    if (size == 0)
    {
        return;
    }

    if (m_RHIBuffer == nullptr)
    {
        RecreateBuffer(data, size);
        return;
    }

    if (data != nullptr && size <= m_Capacity)
    {
        m_RHIBuffer->SetData(data, size);
        return;
    }

    // glVertexAttribPointerは設定時のBuffer object名をVAOへ保持します。
    // 容量拡張でRHIBuffer objectを交換すると既存VAOが古いBufferを参照するため、
    // OpenGL Backendでは同一objectのstorageだけを拡張します。
    m_RHIBuffer->Resize(size, data);
    m_Capacity = size;
}

void OpenGLVertexBuffer::SetLayout(const BufferLayout& layout)
{
    m_Layout = layout;
}

const BufferLayout& OpenGLVertexBuffer::GetLayout() const
{
    return m_Layout;
}

void OpenGLVertexBuffer::RecreateBuffer(const void* data, uint32_t size)
{
    if (size == 0)
    {
        m_RHIBuffer = nullptr;
        m_Capacity = 0;
        return;
    }

    RHIBufferSpecification specification{};
    specification.Size = size;
    specification.Usage = RHIBufferUsage::Vertex;
    specification.MemoryUsage = RHIMemoryUsage::Dynamic;
    specification.DebugName = "Legacy VertexBuffer Bridge";

    m_RHIBuffer = CreateRef<OpenGLRHIBuffer>(specification, data);
    m_Capacity = size;
}

} // namespace Raven
