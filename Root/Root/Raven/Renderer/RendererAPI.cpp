
#include "RendererAPI.h"

namespace Raven
{
    
RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;

RendererAPI::API RendererAPI::GetAPI()
{
    return s_API;
}

}