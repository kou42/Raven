#pragma once

#include "Raven/Renderer/RHI/RHIGraphicsPipelineSpecification.h"
#include "Raven/Renderer/RHI/RHIResource.h"

namespace Raven
{

// Scene用のGPU Graphics Pipelineを表すBackend非依存のResource。
// 既存PipelineはOpenGLのShader/StateをBindするLegacy経路として残す。
// Explicit APIではCommandListがnative pipelineをBindするため、
// このResource自身にはBind/Unbindを設けない。
class RHIGraphicsPipeline : public RHIResource
{
public:
    ~RHIGraphicsPipeline() override = default;

    virtual const RHIGraphicsPipelineSpecification& GetSpecification() const = 0;
};

} // namespace Raven
