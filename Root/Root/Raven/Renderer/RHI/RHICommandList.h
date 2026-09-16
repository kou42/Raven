#pragma once

#include <cstdint>

#include "Raven/Core/Base.h"

namespace Raven
{

class VertexArray;

// ============================================================================
// RHICommandList
// ============================================================================
// 描画命令をGraphics API固有実装から分離するための低レベルCommand interfaceです。
// OpenGLでは各関数を即時実行しますが、DirectX12 / Vulkanでは将来native command listへ
// commandを記録する実装へ置き換えられるよう、Renderer上位層はこのinterfaceだけを参照します。
//
// 現段階では既存RendererAPIから安全に切り離せるViewport / Clear / DrawIndexedのみを扱います。
// Pipeline / Shader / Texture / Uniformは既存Material経路との依存が強いため、後続段階で移行します。
class RHICommandList
{
public:
    virtual ~RHICommandList() = default;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;

    // indexCount == 0 は既存RenderCommandとの互換規約として、
    // VertexArrayに設定されたIndexBuffer全体を描画します。
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
};

} // namespace Raven
