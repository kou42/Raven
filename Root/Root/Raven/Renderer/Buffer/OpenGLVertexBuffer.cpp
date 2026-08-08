#include "Raven/Renderer/Buffer/OpenGLVertexBuffer.h"

#include <glad/glad.h>

namespace Raven
{

OpenGLVertexBuffer::OpenGLVertexBuffer(
    const float* vertices,
    uint32_t size)
{
    glGenBuffers(1, &m_RendererID);

    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);

    glBufferData(
        GL_ARRAY_BUFFER,
        size,
        vertices,
        GL_STATIC_DRAW
    );
}

OpenGLVertexBuffer::~OpenGLVertexBuffer()
{
    glDeleteBuffers(1, &m_RendererID);
}

void OpenGLVertexBuffer::Bind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
}

void OpenGLVertexBuffer::Unbind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
{
    // 頂点データの内容を更新するため、現在の VBO に対して再度バッファを設定する。
    Bind();

    if (data == nullptr)
    {
        glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_STATIC_DRAW);
        return;
    }

    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
}

void OpenGLVertexBuffer::SetLayout(const BufferLayout& layout)
{
    m_Layout = layout;
}

const BufferLayout& OpenGLVertexBuffer::GetLayout() const 
{ 
    return m_Layout; 
}

}