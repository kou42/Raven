#include "Raven/UI/Rendering/UIRenderer.h"

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/UI/Rendering/OpenGLUIRenderer.h"

namespace Raven
{

Scope<UIRenderer> UIRenderer::Create()
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateScope<OpenGLUIRenderer>();
    }

    return nullptr;
}

} // namespace Raven
