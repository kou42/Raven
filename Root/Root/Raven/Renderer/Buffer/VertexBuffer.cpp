#include "VertexBuffer.h"

#include "../RendererAPI.h"
#include "OpenGLVertexBuffer.h"

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