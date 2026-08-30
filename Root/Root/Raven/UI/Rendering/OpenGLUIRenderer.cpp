#include "Raven/UI/Rendering/OpenGLUIRenderer.h"

#include "Raven/Renderer/Buffer/BufferLayout.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/UI/Core/UIDrawList.h"

#include <glad/glad.h>

#include <vector>

namespace Raven
{

OpenGLUIRenderer::OpenGLUIRenderer()
{
    m_VertexArray = VertexArray::Create();
    m_Shader = Shader::Create("Raven/Assets/Shaders/Glsl/UI.glsl");
}

void OpenGLUIRenderer::Render(
    const UIDrawList& drawList,
    const math::Vec2& viewportSize)
{
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        return;
    }

    if (drawList.IsEmpty())
    {
        return;
    }

    if (m_VertexArray == nullptr || m_Shader == nullptr)
    {
        return;
    }

    // ========================================================================
    // DrawList -> dynamic triangle batch
    // ========================================================================
    // SolidRect 1個を4頂点 / 6 indexへ変換し、同一Shaderでまとめて1 Draw Callへ送ります。
    // WidgetごとにDraw Callを発行しないことが、Editor UIで大量のProperty Rowを扱う際に重要です。
    // 頂点形式: Position.xy + Color.rgba = 6 floats
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(drawList.GetCommandCount() * 4u * 6u);
    indices.reserve(drawList.GetCommandCount() * 6u);

    uint32_t vertexBase = 0;

    for (const UIDrawCommand& command : drawList.GetCommands())
    {
        if (command.Type != UIDrawCommandType::SolidRect)
        {
            continue;
        }

        const float left = command.Rect.Min.x;
        const float top = command.Rect.Min.y;
        const float right = command.Rect.Max.x;
        const float bottom = command.Rect.Max.y;

        const auto pushVertex = [&vertices, &command](float x, float y)
        {
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(command.Color.x);
            vertices.push_back(command.Color.y);
            vertices.push_back(command.Color.z);
            vertices.push_back(command.Color.w);
        };

        pushVertex(left, top);
        pushVertex(right, top);
        pushVertex(right, bottom);
        pushVertex(left, bottom);

        indices.push_back(vertexBase + 0u);
        indices.push_back(vertexBase + 1u);
        indices.push_back(vertexBase + 2u);
        indices.push_back(vertexBase + 2u);
        indices.push_back(vertexBase + 3u);
        indices.push_back(vertexBase + 0u);

        vertexBase += 4u;
    }

    if (indices.empty())
    {
        return;
    }

    EnsureBuffers(
        vertices.data(),
        static_cast<uint32_t>(vertices.size() * sizeof(float)),
        indices.data(),
        static_cast<uint32_t>(indices.size()));

    if (m_VertexBuffer == nullptr || m_IndexBuffer == nullptr)
    {
        return;
    }

    // ========================================================================
    // UI render state
    // ========================================================================
    // Raven UIは2D Overlayとして描画するためDepth Testを無効化し、Alpha Blendを有効化します。
    // 描画後には元のEnable状態を復元します。Dear ImGuiも自身のbackendでstateを設定しますが、
    // UI Renderer単体でも呼び出し元へ不要なstateを残さないことを優先します。
    const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_Shader->Bind();
    m_Shader->SetFloat2("u_ViewportSize", viewportSize.x, viewportSize.y);

    m_VertexArray->Bind();
    Renderer::DrawIndexed(m_VertexArray);

    m_VertexArray->Unbind();
    m_Shader->Unbind();

    if (depthTestEnabled == GL_TRUE)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }

    if (blendEnabled == GL_TRUE)
    {
        glEnable(GL_BLEND);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    if (cullFaceEnabled == GL_TRUE)
    {
        glEnable(GL_CULL_FACE);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }
}

void OpenGLUIRenderer::EnsureBuffers(
    const float* vertices,
    uint32_t vertexDataSize,
    const uint32_t* indices,
    uint32_t indexCount)
{
    if (vertexDataSize == 0 || indexCount == 0)
    {
        return;
    }

    if (m_VertexBuffer == nullptr)
    {
        m_VertexBuffer = VertexBuffer::Create(vertices, vertexDataSize);
        if (m_VertexBuffer == nullptr)
        {
            return;
        }

        m_VertexBuffer->SetLayout({
            { ShaderDataType::Float2, "a_Position" },
            { ShaderDataType::Float4, "a_Color" }
        });

        m_VertexArray->AddVertexBuffer(m_VertexBuffer);
    }
    else
    {
        m_VertexBuffer->SetData(vertices, vertexDataSize);
    }

    if (m_IndexBuffer == nullptr)
    {
        m_IndexBuffer = IndexBuffer::Create(indices, indexCount);
        if (m_IndexBuffer == nullptr)
        {
            return;
        }

        m_VertexArray->SetIndexBuffer(m_IndexBuffer);
    }
    else
    {
        m_IndexBuffer->SetData(indices, indexCount);
    }
}

} // namespace Raven
