#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{

// ============================================================================
// FramebufferAttachmentSpecification
// ============================================================================
// Framebufferが所有する1つのAttachmentの形式をRenderer共通のTextureFormatで表します。
// OpenGLのGL_RGBA8 / GL_R32I / GL_DEPTH24_STENCIL8等をここへ持ち込まず、Platform実装側で変換します。
struct FramebufferAttachmentSpecification
{
    FramebufferAttachmentSpecification() = default;
    FramebufferAttachmentSpecification(TextureFormat format)
        : Format(format)
    {
    }

    TextureFormat Format = TextureFormat::None;
};

// ============================================================================
// FramebufferAttachmentList
// ============================================================================
// Color / Depth Attachmentを複数指定できるよう、Attachment一覧をまとめます。
// initializer_list対応により、呼び出し側では
//   { TextureFormat::RGBA8, TextureFormat::R32I, TextureFormat::Depth24Stencil8 }
// のように簡潔にMRT構成を記述できます。
struct FramebufferAttachmentList
{
    FramebufferAttachmentList() = default;
    FramebufferAttachmentList(std::initializer_list<FramebufferAttachmentSpecification> attachments)
        : Attachments(attachments)
    {
    }

    std::vector<FramebufferAttachmentSpecification> Attachments;
};

// ============================================================================
// FramebufferSpecification
// ============================================================================
// FramebufferのサイズとAttachment構成を生成時に明示するための共通設定です。
// Scene Viewだけでなく、Picking / Post Process / HDR / G-Buffer等でも同じFramebuffer基盤を
// 再利用できるよう、具体的なColor/Depth構成をPlatform実装内へ固定しません。
struct FramebufferSpecification
{
    std::uint32_t Width = 1;
    std::uint32_t Height = 1;

    // 現在の既定構成は従来互換のRGBA8 Color + Depth24Stencil8です。
    // Attachmentを明示しない既存コードでも同じ描画結果を維持します。
    FramebufferAttachmentList Attachments =
    {
        TextureFormat::RGBA8,
        TextureFormat::Depth24Stencil8
    };
};

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
// MRTでもColor Attachment番号だけを指定し、GL_COLOR_ATTACHMENT0等のOpenGL定数は
// Platform実装の外へ公開しません。
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

    // 指定indexのColor AttachmentをTextureとして取得します。
    // indexはFramebufferSpecificationに記述したColor Attachmentだけを0から数えた番号です。
    // Depth/Stencil Attachmentはこのindexへ含みません。
    virtual const Ref<Texture>& GetColorAttachment(std::size_t index) const = 0;

    // 現在保持しているColor Attachment数を取得します。
    // MRTを利用する上位Rendererはこの値で有効なindex範囲を確認できます。
    virtual std::size_t GetColorAttachmentCount() const = 0;

    // 指定Color Attachmentの1 pixelを整数値として読み戻します。
    // 現段階ではEntity Picking用途のR32I Attachmentを対象とします。
    // x/yはFramebuffer内のpixel座標です。OpenGL等の座標系差異はPlatform実装側で扱います。
    virtual int ReadPixel(std::size_t attachmentIndex, int x, int y) const = 0;

    // 指定Color Attachment全体を整数値でクリアします。
    // Entity ID Attachmentを毎frame -1で初期化する用途を主目的とします。
    virtual void ClearAttachment(std::size_t attachmentIndex, int value) = 0;

    // 従来互換APIです。Color Attachment 0を返します。
    // Scene/Game View等の単一RenderTarget利用側は既存コードを変更せず利用できます。
    const Ref<Texture>& GetColorAttachment() const
    {
        return GetColorAttachment(0);
    }

    // Framebuffer生成時の設定を取得します。
    // Resize後はWidth / Heightも現在の実サイズへ同期されます。
    virtual const FramebufferSpecification& GetSpecification() const = 0;

    // ------------------------------------------------------------------------
    // Compatibility bridge
    // ------------------------------------------------------------------------
    // EditorのDear ImGui連携は現状ImTextureIDへOpenGL Texture IDを渡しているため、
    // 既存コードを一度に壊さないよう互換APIを一時的に残します。
    // 引数なし版はColor Attachment 0へ委譲します。
    std::uint32_t GetColorAttachmentRendererID() const;
    std::uint32_t GetColorAttachmentRendererID(std::size_t index) const;

    virtual std::uint32_t GetWidth() const = 0;
    virtual std::uint32_t GetHeight() const = 0;

    // 従来互換の生成APIです。
    // 内部ではRGBA8 Color + Depth24Stencil8のFramebufferSpecificationへ変換します。
    static std::unique_ptr<Framebuffer> Create(std::uint32_t width, std::uint32_t height);

    // 現在選択されているRendererAPIに対応したFramebuffer実装をSpecificationから生成します。
    // 上位層がOpenGLFramebuffer等を直接newしないことでPlatform依存をRenderer層へ閉じ込めます。
    static std::unique_ptr<Framebuffer> Create(const FramebufferSpecification& specification);

protected:
    Framebuffer() = default;
};

} // namespace Raven
