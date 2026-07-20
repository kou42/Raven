#pragma once

#include "Raven/Renderer/Pipeline/Pipeline.h"

namespace Raven
{

class OpenGLPipeline final : public Pipeline
{
public:
    explicit OpenGLPipeline(
        const PipelineSpecification& specification
    );

    void Bind() const override;
    void Unbind() const override;

    const PipelineSpecification&
        GetSpecification() const override
    {
        return m_Specification;
    }

    Ref<Shader> GetShader() const override
    {
        return m_Specification.Shader;
    }

private:
    PipelineSpecification m_Specification;
};

}