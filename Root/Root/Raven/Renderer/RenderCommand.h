#pragma once

#include "RendererAPI.h"
#include "../Core/Base.h"

namespace Raven
{

class RenderCommand
{
public:

    static void Init();

    static void SetClearColor(float r, float g, float b, float a);

    static void Clear();

private:
    static Scope<RendererAPI> s_RendererAPI;
};

}