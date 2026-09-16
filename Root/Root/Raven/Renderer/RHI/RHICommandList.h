#pragma once

#include <cstdint>

namespace Raven
{

// ============================================================================
// RHICommandList
// ============================================================================
// 描画命令をGraphics API固有実装から分離するための低レベルCommand interfaceです。
// OpenGLでは各関数を即時実行しますが、DirectX12 / Vulkanでは将来native command listへ
// commandを記録する実装へ置き換えられるよう、Renderer上位層はこのinterfaceだけを参照します。
//
// 現段階では既存RendererAPIから安全に切り離せるViewport / Clear系命令のみを扱います。
// DrawIndexedは現在のPipelineが持つPrimitiveTopologyに依存するため、Pipeline bindingと
// 同時にRHIへ移行します。Shader / Texture / UniformもMaterial移行に合わせて後続対応します。
class RHICommandList
{
public:
    virtual ~RHICommandList() = default;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;
};

} // namespace Raven
