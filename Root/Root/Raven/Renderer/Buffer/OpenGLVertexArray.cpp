#include "Raven/Renderer/Buffer/OpenGLVertexArray.h"

#include <glad/glad.h>

namespace Raven
{

static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
{
    switch (type)
    {
    case ShaderDataType::Float:
    case ShaderDataType::Float2:
    case ShaderDataType::Float3:
    case ShaderDataType::Float4:
    case ShaderDataType::Mat3:
    case ShaderDataType::Mat4:
        return GL_FLOAT;

    case ShaderDataType::Int:
    case ShaderDataType::Int2:
    case ShaderDataType::Int3:
    case ShaderDataType::Int4:
        return GL_INT;

    case ShaderDataType::Bool:
        return GL_BOOL;
    }

    return 0;
}

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
    // 規約:
    // - VBO(GL_ARRAY_BUFFER) は「属性定義(glVertexAttribPointer)時」に現在の VAO へ取り込まれる。
    // - 必ず対象 VAO を Bind() してから、頂点属性の有効化とレイアウト設定を行う。
    Bind();
    vertexBuffer->Bind();

    const auto& layout = vertexBuffer->GetLayout();

    uint32_t index = 0;
    for (const auto& element : layout)
    {
        glEnableVertexAttribArray(index);

        glVertexAttribPointer(
            index,
            element.GetComponentCount(),
            ShaderDataTypeToOpenGLBaseType(element.Type),
            element.Normalized ? GL_TRUE : GL_FALSE,
            layout.GetStride(),
            (const void*)element.Offset
        );

        index++;
    }

    m_VertexBuffers.push_back(vertexBuffer);
}

void OpenGLVertexArray::SetIndexBuffer(
    const Ref<IndexBuffer>& indexBuffer)
{
    // 規約:
    // - EBO(GL_ELEMENT_ARRAY_BUFFER) の bind は VAO 状態そのもの。
    // - EBO の関連付けはこの関数に一本化し、他所(バッファ生成時など)で
    //   GL_ELEMENT_ARRAY_BUFFER を bind しない。
    //   (他の VAO の EBO を意図せず上書きしないため)
    Bind();
    indexBuffer->Bind();

    m_IndexBuffer = indexBuffer;
}

}