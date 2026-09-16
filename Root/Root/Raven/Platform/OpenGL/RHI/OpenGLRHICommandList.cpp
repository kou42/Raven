#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"

#include <type_traits>

#include <glad/glad.h>

#include "Raven/Renderer/Buffer/IndexBuffer.h"
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

void OpenGLRHICommandList::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    glViewport(
        static_cast<GLint>(x),
        static_cast<GLint>(y),
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height));
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

void OpenGLRHICommandList::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
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

    uint32_t resolvedIndexCount = indexCount;
    if (resolvedIndexCount == 0)
    {
        resolvedIndexCount = indexBuffer->GetCount();
    }

    if (resolvedIndexCount == 0)
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
        nullptr);
}

} // namespace Raven
