#include "Raven/UI/Rendering/OpenGLUIRenderer.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/Buffer/BufferLayout.h"
#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Buffer/VertexBuffer.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Texture/Texture.h"
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

void OpenGLUIRenderer::Render(const UIDrawList& drawList, const math::Vec2& viewportSize)
{
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f || drawList.IsEmpty())
    {
        return;
    }
    if (m_VertexArray == nullptr || m_Shader == nullptr)
    {
        return;
    }

    // Texture切替をCommand境界で行えるよう、各Commandを連続したQuadとして同じdynamic bufferへ格納します。
    // 頂点形式は Position.xy + Color.rgba + UV.xy = 8 floats です。
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(drawList.GetCommandCount() * 4u * 8u);
    indices.reserve(drawList.GetCommandCount() * 6u);

    uint32_t vertexBase = 0;
    for (const UIDrawCommand& command : drawList.GetCommands())
    {
        const float left = command.Rect.Min.x;
        const float top = command.Rect.Min.y;
        const float right = command.Rect.Max.x;
        const float bottom = command.Rect.Max.y;

        const auto pushVertex = [&vertices, &command](float x, float y, float u, float v)
        {
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(command.Color.x);
            vertices.push_back(command.Color.y);
            vertices.push_back(command.Color.z);
            vertices.push_back(command.Color.w);

            // TextureAssetImporterはSource画像の先頭rowをそのままTextureのrow 0へuploadします。
            // OpenGLのnormalized Texture座標V=0はそのrow 0をsamplingするため、Ravenの
            // 左上原点UV(V=0が画像上端)をここで反転する必要はありません。
            // Framebufferの画面座標原点と、upload済みTexture内のrow/UV対応は別概念として扱います。
            vertices.push_back(u);
            vertices.push_back(v);
        };

        pushVertex(left, top, command.UVMin.x, command.UVMin.y);
        pushVertex(right, top, command.UVMax.x, command.UVMin.y);
        pushVertex(right, bottom, command.UVMax.x, command.UVMax.y);
        pushVertex(left, bottom, command.UVMin.x, command.UVMax.y);

        indices.push_back(vertexBase + 0u);
        indices.push_back(vertexBase + 1u);
        indices.push_back(vertexBase + 2u);
        indices.push_back(vertexBase + 2u);
        indices.push_back(vertexBase + 3u);
        indices.push_back(vertexBase + 0u);
        vertexBase += 4u;
    }

    EnsureBuffers(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(float)), indices.data(), static_cast<uint32_t>(indices.size()));
    if (m_VertexBuffer == nullptr || m_IndexBuffer == nullptr)
    {
        return;
    }

    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousDrawBuffer = GL_BACK;
    GLint previousReadBuffer = GL_BACK;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    GLint previousScissorBox[4] = { 0, 0, 0, 0 };
    GLint previousPolygonMode[2] = { GL_FILL, GL_FILL };
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTextureBinding = 0;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    glGetIntegerv(GL_POLYGON_MODE, previousPolygonMode);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);

    const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorTestEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean previousColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLboolean previousDepthMask = GL_TRUE;
    GLboolean doubleBuffered = GL_FALSE;
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(doubleBuffered == GL_TRUE ? GL_BACK : GL_FRONT);
    glViewport(0, 0, static_cast<GLsizei>(viewportSize.x), static_cast<GLsizei>(viewportSize.y));
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
    m_Shader->SetInt("u_Texture", 0);
    m_VertexArray->Bind();

    // Image CommandではTextureAsset -> Runtime Textureへの解決をbackend内だけで行います。
    // これによりUIDrawCommand/WidgetへOpenGL Texture IDを公開しません。
    std::size_t commandIndex = 0;
    for (const UIDrawCommand& command : drawList.GetCommands())
    {
        bool useTexture = false;
        if (command.Type == UIDrawCommandType::Image && command.Texture != nullptr && command.Texture->IsValid())
        {
            const Ref<Texture>& texture = command.Texture->GetTexture();
            if (texture != nullptr)
            {
                texture->Bind(0);
                useTexture = true;
            }
        }

        m_Shader->SetInt("u_UseTexture", useTexture ? 1 : 0);
        const void* indexOffset = reinterpret_cast<const void*>(commandIndex * 6u * sizeof(uint32_t));
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, indexOffset);
        ++commandIndex;
    }

    m_VertexArray->Unbind();
    m_Shader->Unbind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glDrawBuffer(static_cast<GLenum>(previousDrawBuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
    glPolygonMode(GL_FRONT, previousPolygonMode[0]);
    glPolygonMode(GL_BACK, previousPolygonMode[1]);
    glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
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

void OpenGLUIRenderer::EnsureBuffers(const float* vertices, uint32_t vertexDataSize, const uint32_t* indices, uint32_t indexCount)
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
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Float2, "a_TexCoord" }
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
