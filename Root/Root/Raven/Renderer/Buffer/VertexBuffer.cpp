#include "Raven/Renderer/Buffer/VertexBuffer.h"

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/Buffer/OpenGLVertexBuffer.h"

namespace Raven
{

Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLVertexBuffer>(vertices, size);
    }

    return nullptr;
}

}