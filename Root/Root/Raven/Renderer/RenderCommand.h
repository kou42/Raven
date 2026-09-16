#pragma once

//RenderCommand は「描画APIへの命令窓口」です。
//Application 側から glClear() などを直接呼ばないための薄いラッパーです。
//RHI移行中は公開APIを維持しつつ、描画命令をRHICommandListへ転送する互換層として機能します。

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHICommandList.h"
#include "Raven/Renderer/Shader/ShaderTypes.h"

namespace Raven
{

class Pipeline;
class RHIDevice;
class RendererAPI;
class Texture;
class VertexArray;

class RenderCommand
{
public:

    static void Init();

    static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    static RHIViewport GetViewport();

    static void SetClearColor(float r, float g, float b, float a);

    static void Clear();

    static void BindPipeline(const Ref<Pipeline>& pipeline);
    static void BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot);
    static void UploadUniform(const std::string& name, const UniformValue& value);

    static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0);

    // GPU Resource生成はCommandListとは責務が異なるためDeviceを経由します。
    // Legacy Renderer/Texture層がBackend実装を直接生成しないための段階移行用窓口です。
    static RHIDevice* GetDevice();

    static RendererAPI& GetAPI();

    static void SetAPI(std::unique_ptr<RendererAPI> api);

private:
    static Scope<RendererAPI> s_RendererAPI;
    static Scope<RHIDevice> s_Device;
    static Scope<RHICommandList> s_CommandList;
};

}
