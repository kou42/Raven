#include "Raven/Renderer/Buffer/IndexBuffer.h"

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/Buffer/OpenGLIndexBuffer.h"


namespace Raven
{

Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices,uint32_t count)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLIndexBuffer>(indices, count);
    }

    return nullptr;
}

}