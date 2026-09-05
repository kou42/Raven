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

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace Raven
{

namespace
{
constexpr uint32_t kCircleSegmentCount = 48u;
constexpr float kTwoPi = 6.28318530717958647692f;

uint32_t GetCommandIndexCount(UIDrawCommandType type)
{
    if (type == UIDrawCommandType::SolidCircle)
    {
        return kCircleSegmentCount * 3u;
    }
    return 6u;
}
} // namespace

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
    // Rect / Imageは4頂点6index、Circleは中心+外周頂点のTriangle Fanへ変換し、
    // 同じdynamic bufferへ格納します。
    // ImageではCommand境界でTextureを切り替えるため現段階では1 Command = 1 Draw Callですが、
    // Widgetが直接GPU Bufferを生成しないというDrawList境界は維持します。
    // CommandごとのIndex数が可変になったため、描画時はCommand種別からoffset/countを進めます。
    // 頂点形式: Position.xy + Color.rgba + UV.xy = 8 floats
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(drawList.GetCommandCount() * 8u * 8u);
    indices.reserve(drawList.GetCommandCount() * kCircleSegmentCount * 3u);

    uint32_t vertexBase = 0u;

    for (const UIDrawCommand& command : drawList.GetCommands())
    {
        const float left = command.Rect.Min.x;
        const float top = command.Rect.Min.y;
        const float right = command.Rect.Max.x;
        const float bottom = command.Rect.Max.y;

        const auto pushVertex = [&vertices, &command](float x, float y, float u, float v)
        {
            // UIElement側で親子Transformを合成済みなので、RendererはWorld Affine Transformを
            // Layout済み頂点へそのまま適用します。Shearを含むTransformもここで失われません。
            // Circleも各tessellation頂点へ同じTransformを適用するため、非一様ScaleやShearでも
            // 円形Command固有の別Transform経路を増やす必要がありません。
            const math::Vec2 transformed = command.Transform.TransformPoint(math::Vec2(x, y));
            vertices.push_back(transformed.x);
            vertices.push_back(transformed.y);
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

        if (command.Type == UIDrawCommandType::SolidCircle)
        {
            const float centerX = (left + right) * 0.5f;
            const float centerY = (top + bottom) * 0.5f;
            const float radiusX = (right - left) * 0.5f;
            const float radiusY = (bottom - top) * 0.5f;

            // SVG/UI Vector Shapeの初期実装では48分割を固定品質とします。
            // Segment数をCommandへ持たせるとWidget/APIへtessellation知識が漏れるため、
            // 品質ポリシーはRenderer backendへ閉じ込めます。
            pushVertex(centerX, centerY, 0.5f, 0.5f);
            for (uint32_t segment = 0u; segment < kCircleSegmentCount; ++segment)
            {
                const float angle =
                    kTwoPi * static_cast<float>(segment) / static_cast<float>(kCircleSegmentCount);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                pushVertex(
                    centerX + cosine * radiusX,
                    centerY + sine * radiusY,
                    0.5f + cosine * 0.5f,
                    0.5f + sine * 0.5f);
            }

            for (uint32_t segment = 0u; segment < kCircleSegmentCount; ++segment)
            {
                const uint32_t current = vertexBase + 1u + segment;
                const uint32_t next = vertexBase + 1u + ((segment + 1u) % kCircleSegmentCount);
                indices.push_back(vertexBase);
                indices.push_back(current);
                indices.push_back(next);
            }

            vertexBase += 1u + kCircleSegmentCount;
            continue;
        }

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
    // EditorはScene View / Game ViewをFramebufferへ描画してから、そのTextureをDear ImGuiで表示します。
    // Dear ImGuiのOpenGL backendは描画後に「呼び出し前のFramebuffer」を復元するため、
    // ImGui::End()直後にRaven UIを描くだけではScene/Game用offscreen framebufferへ描かれる場合があります。
    // その結果、Main Window左上へ出す検証Rectが見えない状態になります。
    //
    // Main Window用UIContextは最終Window Overlayを担当するため、ここではdefault framebuffer(0)と
    // Window全体のviewportを明示的に選択します。将来Game View / RenderTexture用UIContextを追加する際は、
    // UIContext側へRenderTargetを持たせ、この固定0をContext指定のFramebufferへ置き換えます。
    //
    // さらに、直前の3D PipelineがPolygonMode / ColorMask / DepthMaskなどを変更していても
    // UI描画結果が影響を受けないよう、UI backendが必要なstateを明示し、描画後にすべて復元します。
    // Image描画ではTexture Unit 0も変更するため、Active TextureとBindingも同じ方針で保存・復元します。
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

    // Default framebufferがDouble Bufferの場合、画面へ提示されるのは通常Back Bufferです。
    // 直前のoffscreen描画や外部stateでDrawBufferが別値になっていてもUIを正しいBufferへ書くため、
    // Main Window用Contextでは描画先を明示します。
    const GLenum defaultColorBuffer = doubleBuffered == GL_TRUE ? GL_BACK : GL_FRONT;
    glDrawBuffer(defaultColorBuffer);

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
    m_Shader->SetInt("u_Texture", 0);
    m_VertexArray->Bind();

    // ========================================================================
    // UI専用Draw Call
    // ========================================================================
    // Renderer::DrawIndexed()は現在の3D PipelineのPrimitiveTopologyを参照します。
    // UIは常にTriangle Listなので、直前SceneのLine/Point Pipeline状態を継承しないよう、
    // OpenGL backend内でGL_TRIANGLESを明示して直接Drawします。
    //
    // Image CommandではTextureAsset -> Runtime Textureへの解決もbackend内だけで行います。
    // これによりUIDrawCommand / WidgetへOpenGL Texture IDを公開しません。
    // Circle追加後はCommand種別ごとにIndex数が異なるため、固定6indexではなく累積offsetを進めます。
    uint32_t indexOffsetCount = 0u;
    for (const UIDrawCommand& command : drawList.GetCommands())
    {
        if (command.Clip.Enabled == true)
        {
            // Raven UIは左上原点、OpenGL Scissorは左下原点なのでYを反転します。
            // float境界は外側へ丸め、Transform済みAABBの端にあるpixelを誤って欠落させないようにします。
            const float clippedLeft = std::clamp(command.Clip.Rect.Min.x, 0.0f, viewportSize.x);
            const float clippedTop = std::clamp(command.Clip.Rect.Min.y, 0.0f, viewportSize.y);
            const float clippedRight = std::clamp(command.Clip.Rect.Max.x, 0.0f, viewportSize.x);
            const float clippedBottom = std::clamp(command.Clip.Rect.Max.y, 0.0f, viewportSize.y);

            const int leftPixel = static_cast<int>(std::floor(clippedLeft));
            const int topPixel = static_cast<int>(std::floor(clippedTop));
            const int rightPixel = static_cast<int>(std::ceil(clippedRight));
            const int bottomPixel = static_cast<int>(std::ceil(clippedBottom));
            const int viewportHeight = static_cast<int>(viewportSize.y);
            const int scissorWidth = std::max(0, rightPixel - leftPixel);
            const int scissorHeight = std::max(0, bottomPixel - topPixel);
            const int scissorY = viewportHeight - bottomPixel;

            glEnable(GL_SCISSOR_TEST);
            glScissor(
                leftPixel,
                scissorY,
                scissorWidth,
                scissorHeight);
        }
        else
        {
            glDisable(GL_SCISSOR_TEST);
        }

        bool useTexture = false;
        if (command.Type == UIDrawCommandType::Image &&
            command.Texture != nullptr &&
            command.Texture->IsValid())
        {
            const Ref<Texture>& texture = command.Texture->GetTexture();
            if (texture != nullptr)
            {
                texture->Bind(0);
                useTexture = true;
            }
        }

        m_Shader->SetInt("u_UseTexture", useTexture ? 1 : 0);

        const uint32_t indexCount = GetCommandIndexCount(command.Type);
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::size_t>(indexOffsetCount) * sizeof(uint32_t));
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(indexCount),
            GL_UNSIGNED_INT,
            indexOffset);
        indexOffsetCount += indexCount;
    }

    // 以前は初回描画の切り分けとしてglReadPixels()でBack Bufferを読み戻していました。
    // 描画経路が正常であることを確認できたため、通常実行時にGPU同期を発生させないようReadback診断は終了しています。

    m_VertexArray->Unbind();
    m_Shader->Unbind();

    // ========================================================================
    // State restore
    // ========================================================================
    // Raven UIをRenderer pipelineの途中から呼んでも後続描画へ影響を残さないよう、
    // Framebuffer / viewport / scissorに加えて、今回UI側で上書きしたPolygonMode / ColorMask /
    // DepthMask / Texture Bindingも呼び出し前の値へ戻します。UI backendが外部Renderer stateを
    // 漏らさないための処理です。
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glDrawBuffer(static_cast<GLenum>(previousDrawBuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
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
