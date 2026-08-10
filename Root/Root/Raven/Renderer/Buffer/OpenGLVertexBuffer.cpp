#include "Raven/Renderer/Buffer/OpenGLVertexBuffer.h"

#include <glad/glad.h>

namespace Raven
{

OpenGLVertexBuffer::OpenGLVertexBuffer(
    const float* vertices,
    uint32_t size)
    : m_Capacity(size)
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
    Bind();

    if (size == 0)
    {
        return;
    }

    // ========================================================================
    // Dynamic update path
    // ========================================================================
    // 既存容量に収まる通常の頂点変形(Skeletal / SoftBody / Morph)では、VBOを再確保せず
    // glBufferSubDataで内容だけを更新します。Fixed TopologyのDynamic Geometryでは頂点数が
    // 変わらないため、基本的にこの経路を通ります。
    if (data != nullptr && size <= m_Capacity)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
        return;
    }

    // 容量を超える場合、または空データで明示的に領域だけ確保したい場合は再確保します。
    // 再確保後は更新用途であることをDriverへ伝えるためGL_DYNAMIC_DRAWを使用します。
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_DYNAMIC_DRAW);
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

}