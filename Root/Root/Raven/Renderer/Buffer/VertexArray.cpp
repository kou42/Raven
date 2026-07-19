#include "Raven/Renderer/Buffer/VertexArray.h"

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/Buffer/OpenGLVertexArray.h"

namespace Raven
{

Ref<VertexArray> VertexArray::Create()
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLVertexArray>();
    }

    return nullptr;
}

}