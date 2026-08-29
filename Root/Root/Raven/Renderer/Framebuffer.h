#pragma once

#include <cstdint>
#include <memory>

#include "Raven/Core/Base.h"

namespace Raven
{

class Texture;

// ============================================================================
// Framebuffer
// ============================================================================
// Scene View / Game View、将来のPickingやPost Processなどで利用する描画先を表す
// Renderer共通の抽象インターフェースです。
//
// 重要:
// このクラスではglBindFramebuffer等のOpenGL APIを一切扱いません。
// EditorやScene等の上位層はFramebufferだけへ依存し、実際のGPU Resource管理は
// OpenGLFramebuffer / 将来のDirectXFramebuffer等のPlatform実装へ隠蔽します。
//
// Color AttachmentはRenderer固有IDではなくTexture抽象クラスとして公開します。
// これによりFramebufferを利用する側は「描画結果はTextureである」という共通概念だけを扱えます。
// OpenGLのGLuint等、backend固有表現をFramebufferインターフェースへ持ち込みません。
class Framebuffer
{
public:
    virtual ~Framebuffer() = default;

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // このFramebufferを現在の描画先として設定します。
    // 実装側では必要に応じてViewportもAttachmentサイズへ合わせます。
    virtual void Bind() const = 0;

    // 描画先をdefault framebufferへ戻します。
    virtual void Unbind() const = 0;

    // Viewport Window等のサイズ変更へ追従します。
    // 0サイズや同一サイズをどのように扱うかはPlatform実装側で安全に処理します。
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

    // 描画済みColor AttachmentをTextureとして取得します。
    // 参照を返すことで不要なRefのコピーを避けつつ、所有権はFramebuffer側に保持します。
    virtual const Ref<Texture>& GetColorAttachment() const = 0;

    virtual std::uint32_t GetWidth() const = 0;
    virtual std::uint32_t GetHeight() const = 0;

    // 現在選択されているRendererAPIに対応したFramebuffer実装を生成します。
    // 上位層がOpenGLFramebuffer等を直接newしないことでPlatform依存をRenderer層へ閉じ込めます。
    static std::unique_ptr<Framebuffer> Create(std::uint32_t width, std::uint32_t height);

protected:
    Framebuffer() = default;
};

} // namespace Raven
