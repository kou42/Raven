#include "Raven/UI/Rendering/OpenGLUIRenderer.h"

#include "Raven/Renderer/Buffer/BufferLayout.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/UI/Core/UIDrawList.h"

#include <glad/glad.h>

#include <iostream>
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
#ifdef _DEBUG
        static bool missingResourceLogged = false;
        if (missingResourceLogged == false)
        {
            std::cout
                << "[Raven UI] Render resource missing. VAO="
                << (m_VertexArray != nullptr ? "valid" : "null")
                << ", Shader="
                << (m_Shader != nullptr ? "valid" : "null")
                << '\n';
            missingResourceLogged = true;
        }
#endif
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
#ifdef _DEBUG
        static bool missingBufferLogged = false;
        if (missingBufferLogged == false)
        {
            std::cout
                << "[Raven UI] Dynamic buffer creation failed. VBO="
                << (m_VertexBuffer != nullptr ? "valid" : "null")
                << ", EBO="
                << (m_IndexBuffer != nullptr ? "valid" : "null")
                << '\n';
            missingBufferLogged = true;
        }
#endif
        return;
    }

    // ========================================================================
    // UI render target / OpenGL state
    // ========================================================================
    // Main Window用UIContextはdefault framebufferへ最終Overlayとして描画します。
    // Scene View / Game View用FramebufferやDear ImGuiが残したOpenGL stateに依存しないよう、
    // 描画に必要なstateをこのbackend内で明示してから、終了時に元へ戻します。
    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    GLint previousScissorBox[4] = { 0, 0, 0, 0 };
    GLint previousPolygonMode[2] = { GL_FILL, GL_FILL };

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    glGetIntegerv(GL_POLYGON_MODE, previousPolygonMode);

    const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorTestEnabled = glIsEnabled(GL_SCISSOR_TEST);

    GLboolean previousColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(
        0,
        0,
        static_cast<GLsizei>(viewportSize.x),
        static_cast<GLsizei>(viewportSize.y));

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    m_Shader->Bind();
    m_Shader->SetVec2("u_ViewportSize", viewportSize);

    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

    m_VertexArray->Bind();

    // ========================================================================
    // UI専用Draw Call
    // ========================================================================
    // Renderer::DrawIndexed()は現在の3D PipelineのPrimitiveTopologyを参照します。
    // UIは常にTriangle Listなので、直前SceneのLine/Point Pipeline状態を継承しないよう、
    // OpenGL backend内でGL_TRIANGLESを明示して直接Drawします。
    const uint32_t indexCount = m_IndexBuffer->GetCount();
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(indexCount),
        GL_UNSIGNED_INT,
        nullptr);

#ifdef _DEBUG
    static bool firstDrawLogged = false;
    if (firstDrawLogged == false)
    {
        const GLenum error = glGetError();
        std::cout
            << "[Raven UI] First draw: commands=" << drawList.GetCommandCount()
            << ", indices=" << indexCount
            << ", viewport=" << viewportSize.x << "x" << viewportSize.y
            << ", program=" << currentProgram
            << ", glError=0x" << std::hex << static_cast<unsigned int>(error)
            << std::dec << '\n';
        firstDrawLogged = true;
    }
#endif

    m_VertexArray->Unbind();
    m_Shader->Unbind();

    // ========================================================================
    // State restore
    // ========================================================================
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glViewport(
        previousViewport[0],
        previousViewport[1],
        previousViewport[2],
        previousViewport[3]);
    glScissor(
        previousScissorBox[0],
        previousScissorBox[1],
        previousScissorBox[2],
        previousScissorBox[3]);
    glPolygonMode(GL_FRONT, previousPolygonMode[0]);
    glPolygonMode(GL_BACK, previousPolygonMode[1]);
    glColorMask(
        previousColorMask[0],
        previousColorMask[1],
        previousColorMask[2],
        previousColorMask[3]);
    glDepthMask(previousDepthMask);

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

    if (scissorTestEnabled == GL_TRUE)
    {
        glEnable(GL_SCISSOR_TEST);
    }
    else
    {
        glDisable(GL_SCISSOR_TEST);
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
