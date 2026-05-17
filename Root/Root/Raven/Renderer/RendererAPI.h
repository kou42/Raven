#pragma once

#include "Buffer/VertexArray.h"

namespace Raven
{

class RendererAPI
{
public:

    enum class API
    {
        None = 0,
        OpenGL,
        DirectX11,
        DirectX12,
        Vulkan
    };

public:
    virtual ~RendererAPI() = default;

    virtual void Init() = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray) = 0;

    static API GetAPI();

private:
    static API s_API;

};

}