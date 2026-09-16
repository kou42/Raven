#include "Raven/Renderer/Buffer/OpenGLIndexBuffer.h"

#include <glad/glad.h>

#include "Raven/Platform/OpenGL/RHI/OpenGLRHIBuffer.h"

namespace Raven
{

OpenGLIndexBuffer::OpenGLIndexBuffer(const uint32_t* indices, uint32_t count)
{
    RecreateBuffer(indices, count);
}

void OpenGLIndexBuffer::Bind() const
{
    if (m_RHIBuffer == nullptr)
    {
        return;
    }

    // GL_ELEMENT_ARRAY_BUFFERのbindingはVAO状態そのものです。
    // Resource生成・更新では触らず、VertexArray::SetIndexBufferから呼ばれるこの互換経路だけで関連付けます。
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RHIBuffer->GetRendererID());
}

void OpenGLIndexBuffer::Unbind() const
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void OpenGLIndexBuffer::SetData(const uint32_t* indices, uint32_t count)
{
    if (count == 0)
    {
        m_Count = 0;
        return;
    }

    if (m_RHIBuffer != nullptr && indices != nullptr && count <= m_Capacity)
    {
        m_RHIBuffer->SetData(indices, static_cast<std::size_t>(count) * sizeof(uint32_t));
        m_Count = count;
        return;
    }

    RecreateBuffer(indices, count);
}

void OpenGLIndexBuffer::RecreateBuffer(const uint32_t* indices, uint32_t count)
{
    if (count == 0)
    {
        m_RHIBuffer = nullptr;
        m_Count = 0;
        m_Capacity = 0;
        return;
    }

    RHIBufferSpecification specification{};
    specification.Size = static_cast<std::size_t>(count) * sizeof(uint32_t);
    specification.Usage = RHIBufferUsage::Index;
    specification.MemoryUsage = RHIMemoryUsage::Dynamic;
    specification.DebugName = "Legacy IndexBuffer Bridge";

    m_RHIBuffer = CreateRef<OpenGLRHIBuffer>(specification, indices);
    m_Count = count;
    m_Capacity = count;
}

} // namespace Raven
