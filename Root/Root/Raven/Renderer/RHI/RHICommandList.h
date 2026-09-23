#pragma once

#include <cstdint>
#include <string>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Shader/ShaderTypes.h"

namespace Raven
{

class Framebuffer;
class Pipeline;
class Texture;
class VertexArray;

// 現行OpenGL互換経路の描画先を一時退避する不透明なstateです。
// 値はBackendだけが解釈し、上位UIはnative handleやbuffer enumを参照しません。
struct RHIRenderTargetState
{
    uint32_t DrawTarget = 0;
    uint32_t ReadTarget = 0;
    uint32_t DrawBuffer = 0;
    uint32_t ReadBuffer = 0;
};

struct RHIScissor
{
    bool Enabled = false;
    uint32_t X = 0;
    uint32_t Y = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

struct RHIViewport
{
    uint32_t X = 0;
    uint32_t Y = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

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

    // Graphics Backend固有の初期描画stateを設定します。
    // Context生成そのものはPlatform層の責務とし、CommandListは有効なContext上で描画stateだけを初期化します。
    virtual void Init() = 0;

    // Window overlayの描画先を選びます。offscreen targetの所有権やattachment構成はFramebuffer側に残します。
    virtual void BindDefaultRenderTarget() = 0;
    // 既存Framebufferの所有権・Attachment設定を維持し、描画先の選択だけを共通命令化します。
    virtual void BindRenderTarget(const Framebuffer& framebuffer) = 0;
    virtual RHIRenderTargetState CaptureRenderTargetState() const = 0;
    virtual void RestoreRenderTargetState(const RHIRenderTargetState& state) = 0;

    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

    // Debug Overlay等の上位層がGraphics API固有のstate queryを直接行わないための参照APIです。
    // OpenGLでは現在のGL viewportを取得し、Explicit APIではCommandListが保持するstateを返す想定です。
    virtual RHIViewport GetViewport() const = 0;

    // ScissorはFramebufferの左下原点Pixel座標。UIの左上原点からの変換は呼び出し側が行います。
    virtual void SetScissor(bool enabled, uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    // Enabledがfalseの場合も矩形を保持し、Overlay終了時に元のstateを復元できます。
    virtual RHIScissor GetScissor() const = 0;

    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;

    virtual void BindPipeline(const Ref<Pipeline>& pipeline) = 0;
    // 一時Overlay終了時に以前のPipeline追跡を復元します。nullptrは追跡解除です。
    virtual void RestorePipelineBinding(const Ref<Pipeline>& pipeline) = 0;
    virtual void BindTexture(const std::string& name, const Ref<Texture>& texture, uint32_t slot) = 0;
    virtual void UploadUniform(const std::string& name, const UniformValue& value) = 0;

    // indexCount == 0 は既存RenderCommandとの互換規約として、
    // VertexArrayに設定されたIndexBuffer全体を描画します。
    // firstIndexはIndexBuffer内の要素単位offsetです（byte offsetではありません）。
    // indexCount == 0 はfirstIndexから末尾までを描画します。
    virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0, uint32_t firstIndex = 0) = 0;
};

} // namespace Raven
