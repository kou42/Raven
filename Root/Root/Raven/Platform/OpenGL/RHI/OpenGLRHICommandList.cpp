#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"

#include <type_traits>

#include <glad/glad.h>

#include "Raven/Renderer/Buffer/IndexBuffer.h"
#include "Raven/Renderer/Framebuffer.h"
#include "Raven/Renderer/Buffer/VertexArray.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{
namespace
{

GLenum ToOpenGLPrimitiveTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::Triangles:
        return GL_TRIANGLES;
    case PrimitiveTopology::Lines:
        return GL_LINES;
    case PrimitiveTopology::Points:
        return GL_POINTS;
    case PrimitiveTopology::None:
    default:
        return GL_NONE;
    }
}

} // namespace

void OpenGLRHICommandList::Init()
{
    // 旧OpenGLRendererAPI::Init()が担当していた既定stateです。
    // Backend固有stateをRHI側へ集約し、Renderer上位層がLegacy RendererAPI objectを
    // 初期化のためだけに保持しなくても同じ描画条件から開始できるようにします。
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OpenGLRHICommandList::BindRenderTarget(const Framebuffer& framebuffer)
{
    // 既存Framebuffer::BindがFBO選択とAttachmentサイズへのViewport同期を担当します。
    // RHI側でnative IDを再取得せず、既存のMRT/Picking構成をそのまま利用します。
    framebuffer.Bind();
}

void OpenGLRHICommandList::BindDefaultRenderTarget()
{
    // UI OverlayはEditorのScene/Game offscreen targetではなくWindowへ描画します。
    // Draw/Readの両方を切り替え、外部stateの復元は呼び出し側の既存契約に従います。
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Windowの表示先を明示します。Single Buffer contextではFrontへ描画します。
    GLboolean doubleBuffered = GL_FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
    glDrawBuffer(doubleBuffered == GL_TRUE ? GL_BACK : GL_FRONT);
}

RHIRenderTargetState OpenGLRHICommandList::CaptureRenderTargetState() const
{
    GLint drawTarget = 0;
    GLint readTarget = 0;
    GLint readBuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawTarget);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readTarget);
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);

    RHIRenderTargetState state{};
    state.DrawTarget = static_cast<uint32_t>(drawTarget);
    state.ReadTarget = static_cast<uint32_t>(readTarget);
    state.ReadBuffer = static_cast<uint32_t>(readBuffer);

    // MRTの複数color attachmentへの出力先はGL_DRAW_BUFFERだけでは復元できません。
    // FBOごとに保持される全slotを退避し、未使用slotのGL_NONEも含めて保存します。
    GLint maxDrawBuffers = 0;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    for (GLint index = 0; index < maxDrawBuffers; ++index)
    {
        GLint buffer = GL_NONE;
        glGetIntegerv(GL_DRAW_BUFFER0 + index, &buffer);
        state.DrawBuffers.push_back(static_cast<uint32_t>(buffer));
    }
    return state;
}

void OpenGLRHICommandList::RestoreRenderTargetState(const RHIRenderTargetState& state)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.DrawTarget));
    if (state.DrawBuffers.empty() == false)
    {
        if (state.DrawTarget == 0u)
        {
            // Default framebufferはglDrawBuffersへ複数のbufferを指定できません。
            glDrawBuffer(static_cast<GLenum>(state.DrawBuffers.front()));
        }
        else
        {
            std::vector<GLenum> buffers;
            buffers.reserve(state.DrawBuffers.size());
            for (uint32_t buffer : state.DrawBuffers)
            {
                buffers.push_back(static_cast<GLenum>(buffer));
            }
            glDrawBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        }
    }

    // Read targetとRead bufferはDraw側と独立して復元します。
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.ReadTarget));
    glReadBuffer(static_cast<GLenum>(state.ReadBuffer));
}

void OpenGLRHICommandList::SetViewport(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    glViewport(
        static_cast<GLint>(x),
        static_cast<GLint>(y),
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height));
}

RHIViewport OpenGLRHICommandList::GetViewport() const
{
    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);

    RHIViewport result{};
    result.X = viewport[0];
    result.Y = viewport[1];
    result.Width = viewport[2] > 0 ? static_cast<uint32_t>(viewport[2]) : 0u;
    result.Height = viewport[3] > 0 ? static_cast<uint32_t>(viewport[3]) : 0u;
    return result;
}

void OpenGLRHICommandList::SetScissor(bool enabled, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if (enabled == false)
    {
        glDisable(GL_SCISSOR_TEST);
        return;
    }

    // glScissorは左下原点のPixel矩形を受け取ります。座標変換は上位UI層の責務です。
    glScissor(static_cast<GLint>(x), static_cast<GLint>(y),
        static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glEnable(GL_SCISSOR_TEST);
}

RHIScissor OpenGLRHICommandList::GetScissor() const
{
    GLint box[4] = {};
    glGetIntegerv(GL_SCISSOR_BOX, box);

    RHIScissor result{};
    result.Enabled = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    result.X = box[0];
    result.Y = box[1];
    result.Width = box[2] > 0 ? static_cast<uint32_t>(box[2]) : 0u;
    result.Height = box[3] > 0 ? static_cast<uint32_t>(box[3]) : 0u;
    return result;
}

void OpenGLRHICommandList::SetClearColor(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
}

void OpenGLRHICommandList::Clear()
{
    // 直前のTransparent pass等でDepth Writeが無効化されている可能性があります。
    // Depth Bufferを確実にclearするため、既存OpenGLRendererAPIと同じく明示的に有効化します。
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRHICommandList::BindPipeline(const Ref<Pipeline>& pipeline)
{
    if (pipeline == nullptr)
    {
        return;
    }

    // 現段階ではLegacy PipelineがOpenGL stateとShader bindingを適用します。
    // CommandList側で現在Pipelineを保持することで、DrawIndexed / Texture / Uniformは
    // RendererAPIへ戻らず同じPipeline stateを基準に処理できます。
    pipeline->Bind();
    m_CurrentPipeline = pipeline;
}

void OpenGLRHICommandList::RestorePipelineBinding(const Ref<Pipeline>& pipeline)
{
    // Pipelineのnative stateはOverlay側が別途復元します。ここではDraw topologyとUniform解決の
    // 追跡だけ戻し、復元済みのBlend/Depth/Shaderを再度上書きしません。
    m_CurrentPipeline = pipeline;
}

void OpenGLRHICommandList::BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot)
{
    if (texture == nullptr || m_CurrentPipeline == nullptr)
    {
        return;
    }

    const Ref<Shader> shader = m_CurrentPipeline->GetShader();
    if (shader == nullptr)
    {
        return;
    }

    // Texture objectのnative bindingは現行Texture abstractionへ委譲します。
    // Sampler / DescriptorをRHI化する段階で、このLegacy Texture依存をRHI Resourceへ置き換えます。
    texture->Bind(slot);
    shader->SetInt(name, static_cast<int>(slot));
}

void OpenGLRHICommandList::UploadUniform(const std::string& name, const UniformValue& value)
{
    if (m_CurrentPipeline == nullptr)
    {
        return;
    }

    const Ref<Shader> shader = m_CurrentPipeline->GetShader();
    if (shader == nullptr)
    {
        return;
    }

    // 現行UniformValueをOpenGL Shader setterへ変換する互換Bridgeです。
    // 将来Constant/Uniform BufferとDescriptor bindingを導入した後は、Material parameterを
    // BufferへpackしてCommandListからResource bindingする経路へ置き換えます。
    std::visit([&](const auto& uniform)
    {
        using T = std::decay_t<decltype(uniform)>;

        if constexpr (std::is_same_v<T, int>)
        {
            shader->SetInt(name, uniform);
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            shader->SetFloat(name, uniform);
        }
        else if constexpr (std::is_same_v<T, math::Vec2>)
        {
            shader->SetVec2(name, uniform);
        }
        else if constexpr (std::is_same_v<T, math::Vec3>)
        {
            shader->SetVec3(name, uniform);
        }
        else if constexpr (std::is_same_v<T, math::Vec4>)
        {
            shader->SetVec4(name, uniform);
        }
        else if constexpr (std::is_same_v<T, math::Mat4>)
        {
            shader->SetMat4(name, uniform);
        }
    }, value);
}

void OpenGLRHICommandList::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t firstIndex)
{
    if (vertexArray == nullptr)
    {
        return;
    }

    const Ref<IndexBuffer>& indexBuffer = vertexArray->GetIndexBuffer();
    if (indexBuffer == nullptr)
    {
        return;
    }

    // UIのCommand別描画に備え、IndexBufferの部分範囲を安全に扱います。
    if (firstIndex > indexBuffer->GetCount())
    {
        return;
    }

    const uint32_t remainingCount = indexBuffer->GetCount() - firstIndex;
    const uint32_t resolvedIndexCount = indexCount == 0 ? remainingCount : indexCount;
    if (resolvedIndexCount == 0 || resolvedIndexCount > remainingCount)
    {
        return;
    }

    // 明示Index数がBuffer容量を超えるDrawはOpenGLへ送らないようにします。
    if (resolvedIndexCount > indexBuffer->GetCount())
    {
        return;
    }

    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    if (m_CurrentPipeline != nullptr)
    {
        topology = m_CurrentPipeline->GetSpecification().Topology;
    }

    const GLenum primitive = ToOpenGLPrimitiveTopology(topology);
    if (primitive == GL_NONE)
    {
        return;
    }

    vertexArray->Bind();
    glDrawElements(
        primitive,
        static_cast<GLsizei>(resolvedIndexCount),
        GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(static_cast<std::size_t>(firstIndex) * sizeof(uint32_t)));
}

} // namespace Raven
