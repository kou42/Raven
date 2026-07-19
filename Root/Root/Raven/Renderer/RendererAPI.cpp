
#include "Raven/Renderer/RendererAPI.h"

namespace Raven
{

#if 0
RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
#else
RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
#endif

RendererAPI::API RendererAPI::GetAPI()
{
    return s_API;
}

}