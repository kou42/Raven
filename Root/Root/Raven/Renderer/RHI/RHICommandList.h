#pragma once

#include <cstdint>

#include "Raven/Core/Base.h"

namespace Raven
{

class Pipeline;
class VertexArray;

// ============================================================================
// RHICommandList
// ============================================================================
// 描画命令をGraphics API固有実装から分離するための低レベルCommand interfaceです。
// OpenGLでは各関数を即時実行しますが、DirectX12 / Vulkanでは将来native command listへ
// commandを記録する実装へ置き換えられるよう、Renderer上位層はこのinterfaceだけを参照します。
//
// Pipeline bindingとDrawIndexedはPrimitiveTopologyを共有する必要があるため同じCommandListで扱います。
// 現段階のPipeline自体はLegacy abstractionですが、描画stateとDraw commandの責務を先にRHI境界へ
// 移すことで、後続のRHIPipeline導入時に上位Rendererを再変更せず置き換えられる構成にします。
class RHICommandList
{
public:
    virtual ~RHICommandList() = default;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;

    virtual void BindPipeline(const Ref<Pipeline>& pipeline) = 0;

    // indexCount == 0 は既存RenderCommandとの互換規約として、
    // VertexArrayに設定されたIndexBuffer全体を描画します。
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
};

} // namespace Raven
