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

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

    // 描画済みColor AttachmentをTextureとして取得します。
    // 参照を返すことで不要なRefのコピーを避けつつ、所有権はFramebuffer側に保持します。
    virtual const Ref<Texture>& GetColorAttachment() const = 0;

    // ------------------------------------------------------------------------
    // Compatibility bridge
    // ------------------------------------------------------------------------
    // EditorのDear ImGui連携は現状ImTextureIDへOpenGL Texture IDを渡しているため、
    // 既存コードを一度に壊さないよう互換APIを一時的に残します。
    // 実際のColor Attachment所有はすでにTextureへ移行済みで、この関数はTexture::GetID()へ
    // 委譲するだけです。ImGui Texture表示をRenderer側へ抽象化した段階で削除します。
    std::uint32_t GetColorAttachmentRendererID() const;

    virtual std::uint32_t GetWidth() const = 0;
    virtual std::uint32_t GetHeight() const = 0;

    static std::unique_ptr<Framebuffer> Create(std::uint32_t width, std::uint32_t height);

protected:
    Framebuffer() = default;
};

} // namespace Raven
