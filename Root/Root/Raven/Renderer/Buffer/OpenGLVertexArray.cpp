#include "OpenGLVertexArray.h"

#include <glad/glad.h>

namespace Raven
{

OpenGLVertexArray::OpenGLVertexArray()
{
    glGenVertexArrays(1, &m_RendererID);
}

OpenGLVertexArray::~OpenGLVertexArray()
{
    glDeleteVertexArrays(1, &m_RendererID);
}

void OpenGLVertexArray::Bind() const
{
    glBindVertexArray(m_RendererID);
}

void OpenGLVertexArray::Unbind() const
{
    glBindVertexArray(0);
}

void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
{
    Bind();
    vertexBuffer->Bind();

    // position
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        (const void*)0);

    // color
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        (const void*)(3 * sizeof(float))
    );

    m_VertexBuffers.push_back(vertexBuffer);
}

void OpenGLVertexArray::SetIndexBuffer(
    const Ref<IndexBuffer>& indexBuffer)
{
    Bind();
    indexBuffer->Bind();

    m_IndexBuffer = indexBuffer;
}

}