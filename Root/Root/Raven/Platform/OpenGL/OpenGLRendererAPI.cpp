#include "Raven/Platform/OpenGL/OpenGLRendererAPI.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>


namespace Raven
{

void OpenGLRendererAPI::Init()
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y,uint32_t width, uint32_t height)
{
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void OpenGLRendererAPI::SetClearColor(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
}

void OpenGLRendererAPI::Clear()
{
    // 直前のパスで深度書き込みが無効化される場合があります (glDepthMask(GL_FALSE))。
    // 毎フレームの深度クリアを確実に有効化するため、ここで明示的に戻します。
    glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
#if 1
    if (!vertexArray || !vertexArray->GetIndexBuffer())
    {
        return;
    }

    GLenum primitive = GL_TRIANGLES;

    if (m_CurrentPipeline)
    {
		const PrimitiveTopology topology = m_CurrentPipeline->GetSpecification().Topology;
        switch (topology)
        {
        case PrimitiveTopology::Triangles:
            primitive = GL_TRIANGLES;
            break;

        case PrimitiveTopology::Lines:
            primitive = GL_LINES;
            break;

        case PrimitiveTopology::Points:
            primitive = GL_POINTS;
            break;

        default:
            return;
        }
    }

    vertexArray->Bind();

	uint32_t count = vertexArray->GetIndexBuffer()->GetCount();
    glDrawElements(primitive, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);

#else
    if (!vertexArray) {
        return;
    }

    if (m_CurrentVertexArray != vertexArray) {
        vertexArray->Bind();
        m_CurrentVertexArray = vertexArray;
    }

    const auto& indexBuffer = vertexArray->GetIndexBuffer();

    if (!indexBuffer)
        return;

    const uint32_t count = (indexCount != 0) ? indexCount : indexBuffer->GetCount();

    if (count == 0) {
        return;
    }

    //vertexArray->Bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr );

#endif
 
}

void OpenGLRendererAPI::BindShader(const Ref<Shader>& shader)
{
    if (!shader) {
        return;
    }
    if (m_CurrentShader == shader) {
        return;
    }

    m_CurrentShader = shader;
    m_CurrentShader->Bind();
}

void OpenGLRendererAPI::BindPipeline(const Ref<Pipeline>& pipeline)
{
    if (!pipeline) {
        return;
    }

    pipeline->Bind();
    m_CurrentPipeline = pipeline;

}

void OpenGLRendererAPI::BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot)
{
#if 1
    if (!texture || !m_CurrentPipeline) {
        return;
    }

    texture->Bind(slot);

    const Ref<Shader> shader = m_CurrentPipeline->GetShader();

    if (shader) {
        shader->SetInt(name, static_cast<int>(slot));
    }
#else
    if (!texture || !m_CurrentShader) {
        return;
    }

    texture->Bind(slot);
    m_CurrentShader->SetInt(name, static_cast<int>(slot));
#endif
}

void OpenGLRendererAPI::UploadUniform(const std::string& name, const UniformValue& value)
{

#if 1

    if (!m_CurrentPipeline) {
        return;
    }

    const Ref<Shader> shader = m_CurrentPipeline->GetShader();

    if (!shader) {
        return;
    }

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
        else if constexpr (
            std::is_same_v<T, math::Vec2>)
        {
            shader->SetVec2(name, uniform);
        }
        else if constexpr (
            std::is_same_v<T, math::Vec3>)
        {
            shader->SetVec3(name, uniform);
        }
        else if constexpr (
            std::is_same_v<T, math::Vec4>)
        {
            shader->SetVec4(name, uniform);
        }
        else if constexpr (
            std::is_same_v<T, math::Mat4>)
        {
            shader->SetMat4(name, uniform);
        }
    }, value);
#else

    if (!m_CurrentShader) {
        return;
    }

    std::visit([&](const auto& v)
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>) {
            m_CurrentShader->SetInt(name, v);
        }
        else if constexpr (std::is_same_v<T, float>) {
            m_CurrentShader->SetFloat(name, v);
        }
        else if constexpr (std::is_same_v<T, math::Vec2>) {
            m_CurrentShader->SetVec2(name, v);
        }
        else if constexpr (std::is_same_v<T, math::Vec3>) {
            m_CurrentShader->SetVec3(name, v);
        }
        else if constexpr (std::is_same_v<T, math::Vec4>) {
            m_CurrentShader->SetVec4(name, v);
        }
        else if constexpr (std::is_same_v<T, math::Mat4>) {
            m_CurrentShader->SetMat4(name, v);
        }
    }, value);
#endif
}

}