#include "VertexArray.h"

#include "../RendererAPI.h"
#include "OpenGLVertexArray.h"

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