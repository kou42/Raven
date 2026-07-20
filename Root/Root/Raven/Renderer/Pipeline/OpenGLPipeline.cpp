#include "Raven/Renderer/Pipeline/OpenGLPipeline.h"

#include <cassert>

#include <glad/glad.h>

#include "Raven/Renderer/Shader/Shader.h"

namespace Raven
{

namespace
{

GLenum ToOpenGLDepthFunction(DepthCompareOperator compareOperator)
{
    switch (compareOperator)
    {
    case DepthCompareOperator::Never:
        return GL_NEVER;

    case DepthCompareOperator::Less:
        return GL_LESS;

    case DepthCompareOperator::LessEqual:
        return GL_LEQUAL;

    case DepthCompareOperator::Equal:
        return GL_EQUAL;

    case DepthCompareOperator::Greater:
        return GL_GREATER;

    case DepthCompareOperator::GreaterEqual:
        return GL_GEQUAL;

    case DepthCompareOperator::Always:
        return GL_ALWAYS;

    default:
        assert(false && "Unknown depth compare operator");
        return GL_LESS;
    }
}

GLenum ToOpenGLFrontFace(FrontFace frontFace)
{
    switch (frontFace)
    {
    case FrontFace::Clockwise:
        return GL_CW;

    case FrontFace::CounterClockwise:
        return GL_CCW;

    default:
        assert(false && "Unknown front face");
        return GL_CCW;
    }
}

GLenum ToOpenGLCullFace(CullMode cullMode)
{
    switch (cullMode)
    {
    case CullMode::Front:
        return GL_FRONT;

    case CullMode::Back:
        return GL_BACK;

    case CullMode::None:
    default:
        assert(false && "Invalid cull mode");
        return GL_BACK;
    }
}

} // end namespace

OpenGLPipeline::OpenGLPipeline(const PipelineSpecification& specification)
{
    assert(m_Specification.Shader &&"Pipeline requires a shader");
    m_Specification = specification;
}

void OpenGLPipeline::Bind() const
{
    if (!m_Specification.Shader) {
        return;
    }

    m_Specification.Shader->Bind();

    // Depth test
    if (m_Specification.DepthTest)
    {
        glEnable(GL_DEPTH_TEST);

        glDepthFunc(ToOpenGLDepthFunction(m_Specification.DepthCompare));
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }

    glDepthMask(m_Specification.DepthWrite ? GL_TRUE : GL_FALSE);

    // Blend
    if (m_Specification.Blend)
    {
        glEnable(GL_BLEND);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    // Culling
    if (m_Specification.Cull == CullMode::None)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);

        glCullFace(ToOpenGLCullFace(m_Specification.Cull));

        glFrontFace(ToOpenGLFrontFace(m_Specification.FrontFaceMode));
    }
}

void OpenGLPipeline::Unbind() const
{
    if (m_Specification.Shader) {
        m_Specification.Shader->Unbind();
    }
}

} // namespace Raven