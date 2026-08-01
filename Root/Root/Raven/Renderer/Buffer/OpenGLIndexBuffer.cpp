#include "Raven/Renderer/Buffer/OpenGLIndexBuffer.h"

#include <glad/glad.h>

namespace Raven
{

OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
{
    m_Count = count;

    glGenBuffers(1, &m_RendererID);

    // ここで GL_ELEMENT_ARRAY_BUFFER を bind しないこと。
    // EBO の bind 先は VAO の状態に紐づくため、ここで bind すると現在の VAO の EBO を上書きしてしまう。
    // まず GL_ARRAY_BUFFER 経由でデータだけをアップロードし、
    // EBO の関連付けは VertexArray::SetIndexBuffer() 側でのみ行う。
    glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);

    glBufferData(
        GL_ARRAY_BUFFER,
        count * sizeof(uint32_t),
        indices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

OpenGLIndexBuffer::~OpenGLIndexBuffer()
{
    glDeleteBuffers(1, &m_RendererID);
}

void OpenGLIndexBuffer::Bind() const
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
}

void OpenGLIndexBuffer::Unbind() const
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

}