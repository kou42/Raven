#include "Raven/Renderer/Pipeline/Pipeline.h"

#include <cassert>

#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Platform/OpenGL/OpenGLPipeline.h"

namespace Raven
{

Ref<Pipeline> Pipeline::Create(const PipelineSpecification& specification)
{
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLPipeline>(specification);

    case RHIBackend::DirectX11:
        assert(false && "RHI Backend is not implemented");
        return nullptr;

    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
        // Explicit Backendのnative PipelineはRuntimeがRHIDeviceから生成します。
        // Material側はPipelineSpecificationを保持できればScene Queueへ参加できるため、
        // Legacy OpenGL Pipelineを要求せず、Backend非依存のSpecification holderを返します。
        return CreateRef<SpecificationPipeline>(specification);

    case RHIBackend::None:
    default:
        assert(false && "RHI Backend is None");
        return nullptr;
    }
}

}