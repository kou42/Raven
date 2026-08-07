#include "Raven/Renderer/Buffer/OpenGLIndexBuffer.h"

#include <glad/glad.h>

namespace Raven
{

OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
{
    m_Count = count;

    glGenBuffers(1, &m_RendererID);

    // GL_ELEMENT_ARRAY_BUFFER は VAO 状態に紐づくため、
    // 一時的に VAO 0 を bind してからデータを初期化します。
    GLint previousVAO = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVAO);
    glBindVertexArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        count * sizeof(uint32_t),
        indices,
        GL_STATIC_DRAW
    );
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glBindVertexArray(static_cast<GLuint>(previousVAO));
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