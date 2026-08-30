#include "Raven/Renderer/Buffer/OpenGLIndexBuffer.h"

#include <glad/glad.h>

namespace Raven
{

OpenGLIndexBuffer::OpenGLIndexBuffer(const uint32_t* indices, uint32_t count)
    : m_Count(count),
      m_Capacity(count)
{
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

void OpenGLIndexBuffer::SetData(const uint32_t* indices, uint32_t count)
{
    if (count == 0)
    {
        m_Count = 0;
        return;
    }

    Bind();

    // ========================================================================
    // Dynamic update path
    // ========================================================================
    // 既存容量へ収まる場合はBuffer Object自体を再生成せず、内容だけを更新します。
    // UI DrawListは毎frame再構築されるため、このFast Pathが通常経路になります。
    if (indices != nullptr && count <= m_Capacity)
    {
        glBufferSubData(
            GL_ELEMENT_ARRAY_BUFFER,
            0,
            count * sizeof(uint32_t),
            indices);

        m_Count = count;
        return;
    }

    // 容量不足時のみGPU領域を拡張します。
    // 以降の更新は新しい容量内でglBufferSubDataへ戻ります。
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        count * sizeof(uint32_t),
        indices,
        GL_DYNAMIC_DRAW);

    m_Count = count;
    m_Capacity = count;
}

}
