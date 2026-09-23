#pragma once

//RenderCommand は「描画APIへの命令窓口」です。
//Application 側から glClear() などを直接呼ばないための薄いラッパーです。
//RHI移行中は公開APIを維持しつつ、描画命令をRHICommandListへ転送する互換層として機能します。

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHICommandList.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/Shader/ShaderTypes.h"

namespace Raven
{

class Pipeline;
class RHIDevice;
class Texture;
class VertexArray;

class RenderCommand
{
public:

    static void Init();

    // Windowが選択したBackendを明示的に受け取り、未対応時はfalseを返します。
    // 引数なし版は既存互換用としてGetRHIBackend()を使用します。
    static bool TryInit(RHIBackend backend);

    static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    static RHIViewport GetViewport();

    static void SetClearColor(float r, float g, float b, float a);

    static void Clear();

    static void BindPipeline(const Ref<Pipeline>& pipeline);
    static void BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot);
    static void UploadUniform(const std::string& name, const UniformValue& value);

    // firstIndexはIndexBufferの要素単位offset。0 countは指定位置から末尾までです。
    static void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0, uint32_t firstIndex = 0);

    // GPU Resource生成はCommandListとは責務が異なるためDeviceを経由します。
    // Legacy Renderer/Texture層がBackend実装を直接生成しないための段階移行用窓口です。
    static RHIDevice* GetDevice();

private:
    static Scope<RHIDevice> s_Device;
    static Scope<RHICommandList> s_CommandList;

    // Draw統計は実際にbindされているPipelineのTopologyを基準に集計します。
    // Backend固有stateをRendererへ問い合わせず、RenderCommandが発行した命令列と同じ状態を保持します。
    static Ref<Pipeline> s_CurrentPipeline;
};

}
