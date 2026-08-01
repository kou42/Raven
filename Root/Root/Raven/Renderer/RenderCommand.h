#pragma once

//RenderCommand は「描画APIへの命令窓口」です。
//Application 側から glClear() などを直接呼ばないための薄いラッパーです。

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Core/Base.h"

namespace Raven
{

class RendererAPI;
class VertexArray;

class RenderCommand
{
public:

    static void Init();

    static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    static void SetClearColor(float r, float g, float b, float a);

    static void Clear();

    static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0);

    static RendererAPI& GetAPI();

    static void SetAPI(std::unique_ptr<RendererAPI> api);

private:
    static Scope<RendererAPI> s_RendererAPI;
};

}