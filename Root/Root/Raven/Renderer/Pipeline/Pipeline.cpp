#include "Raven/Renderer/Pipeline/Pipeline.h"

#include <cassert>

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/Pipeline/OpenGLPipeline.h"

namespace Raven
{

Ref<Pipeline> Pipeline::Create(const PipelineSpecification& specification)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLPipeline>(specification);

    case RendererAPI::API::DirectX11:
    case RendererAPI::API::DirectX12:
    case RendererAPI::API::Vulkan:
        assert(false && "Renderer API is not implemented");
        return nullptr;

    case RendererAPI::API::None:
    default:
        assert(false && "Renderer API is None");
        return nullptr;
    }
}

}