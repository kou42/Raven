#pragma once

#include <cstdint>
#include <string>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Shader/ShaderTypes.h"

namespace Raven
{

class Pipeline;
class Texture;
class VertexArray;

// ============================================================================
// RHICommandList
// ============================================================================
// 描画命令をGraphics API固有実装から分離するための低レベルCommand interfaceです。
// OpenGLでは各関数を即時実行しますが、DirectX12 / Vulkanでは将来native command listへ
// commandを記録する実装へ置き換えられるよう、Renderer上位層はこのinterfaceだけを参照します。
//
// Pipeline bindingとDrawIndexedはPrimitiveTopologyを共有する必要があるため同じCommandListで扱います。
// Texture / Uniformも現在PipelineのShaderへ適用するためCommandListへ集約し、MaterialがLegacy
// RendererAPIへ依存せず描画Resourceを設定できる境界へ段階的に移行します。
class RHICommandList
{
public:
    virtual ~RHICommandList() = default;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;

    virtual void BindPipeline(const Ref<Pipeline>& pipeline) = 0;
    virtual void BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot) = 0;
    virtual void UploadUniform(const std::string& name, const UniformValue& value) = 0;

    // indexCount == 0 は既存RenderCommandとの互換規約として、
    // VertexArrayに設定されたIndexBuffer全体を描画します。
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
};

} // namespace Raven
