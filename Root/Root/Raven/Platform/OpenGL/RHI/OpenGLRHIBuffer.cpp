#include "Raven/Platform/OpenGL/RHI/OpenGLRHIBuffer.h"

#include <glad/glad.h>

namespace Raven
{
namespace
{
GLenum ToOpenGLBufferUsage(RHIMemoryUsage usage)
{
    switch (usage)
    {
    case RHIMemoryUsage::Static:
        return GL_STATIC_DRAW;
    case RHIMemoryUsage::Dynamic:
        return GL_DYNAMIC_DRAW;
    }

    return GL_STATIC_DRAW;
}
} // namespace

OpenGLRHIBuffer::OpenGLRHIBuffer(
    const RHIBufferSpecification& specification,
    const void* initialData)
    : m_Specification(specification)
{
    glGenBuffers(1, &m_RendererID);

    // GL_COPY_WRITE_BUFFERはVAO状態へ影響しない汎用Buffer targetです。
    // Index Buffer生成時にGL_ELEMENT_ARRAY_BUFFERへ直接bindすると現在のVAO状態を書き換えるため、
    // Resource生成・更新では用途に関係なくこのtargetを使用します。
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_RendererID);
    glBufferData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLsizeiptr>(m_Specification.Size),
        initialData,
        ToOpenGLBufferUsage(m_Specification.MemoryUsage));
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

OpenGLRHIBuffer::~OpenGLRHIBuffer()
{
    if (m_RendererID != 0)
    {
        glDeleteBuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

void OpenGLRHIBuffer::SetData(const void* data, std::size_t size, std::size_t offset)
{
    if (data == nullptr || size == 0)
    {
        return;
    }

    // Buffer範囲外への書き込みはGraphics APIへ渡しません。
    // subtraction形式で検証し、offset + sizeの整数overflowも避けます。
    if (offset > m_Specification.Size || size > m_Specification.Size - offset)
    {
        return;
    }

    glBindBuffer(GL_COPY_WRITE_BUFFER, m_RendererID);
    glBufferSubData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLintptr>(offset),
        static_cast<GLsizeiptr>(size),
        data);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

const RHIBufferSpecification& OpenGLRHIBuffer::GetSpecification() const
{
    return m_Specification;
}

void OpenGLRHIBuffer::Resize(std::size_t size, const void* data)
{
    if (size == 0)
    {
        return;
    }

    if (size == m_Specification.Size)
    {
        if (data != nullptr)
        {
            SetData(data, size);
        }
        return;
    }

    // OpenGL object名を維持したままstorageだけを再確保します。
    // GL_COPY_WRITE_BUFFERを使うことで、現在bind中のVAOが保持するEBO関連付けも変更しません。
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_RendererID);
    glBufferData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLsizeiptr>(size),
        data,
        ToOpenGLBufferUsage(m_Specification.MemoryUsage));
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    m_Specification.Size = size;
}

std::uint32_t OpenGLRHIBuffer::GetRendererID() const
{
    return m_RendererID;
}

} // namespace Raven
