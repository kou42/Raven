#include "Raven/Renderer/Pipeline/Pipeline.h"

#include <cassert>

#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/Pipeline/OpenGLPipeline.h"

namespace Raven
{

Ref<Pipeline> Pipeline::Create(const PipelineSpecification& specification)
{
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLPipeline>(specification);

    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
        assert(false && "RHI Backend is not implemented");
        return nullptr;

    case RHIBackend::None:
    default:
        assert(false && "RHI Backend is None");
        return nullptr;
    }
}

}