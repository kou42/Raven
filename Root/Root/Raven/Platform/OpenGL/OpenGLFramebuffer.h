#pragma once

#include "Raven/Renderer/Framebuffer.h"

#include <cstdint>

namespace Raven
{

class Texture;

// ============================================================================
// OpenGLFramebuffer
// ============================================================================
// Framebuffer抽象インターフェースのOpenGL実装です。
// OpenGL固有のFBO IDはこのクラスだけが所有します。
//
// Color Attachment:
//   FramebufferSpecificationで指定されたColor FormatからRenderTarget用途のTextureを生成します。
//   現段階では描画経路との互換性を優先し、Color Attachmentは1枚まで対応します。
//
// Depth/Stencil Attachment:
//   Depth24Stencil8が指定された場合はDepthStencil用途のTexture抽象クラスとして保持します。
//   Color/DepthともGPU Texture Resourceの生成・破棄責務をTextureへ統一し、
//   Framebufferは「どのTextureをどのAttachmentへ接続するか」だけを担当します。
class OpenGLFramebuffer final : public Framebuffer
{
public:
    // 従来互換コンストラクタです。
    // 内部では既定Attachment構成を持つFramebufferSpecificationへ変換します。
    OpenGLFramebuffer(std::uint32_t width, std::uint32_t height);

    explicit OpenGLFramebuffer(const FramebufferSpecification& specification);
    ~OpenGLFramebuffer() override;

    OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
    OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;

    void Bind() const override;
    void Unbind() const override;
    void Resize(std::uint32_t width, std::uint32_t height) override;

    const Ref<Texture>& GetColorAttachment() const override { return m_ColorAttachment; }
    const FramebufferSpecification& GetSpecification() const override { return m_Specification; }
    std::uint32_t GetWidth() const override { return m_Specification.Width; }
    std::uint32_t GetHeight() const override { return m_Specification.Height; }

private:
    // 現在のSpecificationを使ってOpenGL Resource一式を再生成します。
    // Resize時にも同じ処理を使うため、生成処理を一箇所へ集約します。
    void Invalidate();

    // 所有しているOpenGL Resourceを安全に解放します。
    // Texture AttachmentはRefのreset()によりTextureクラス自身のRAIIへ解放を委譲します。
    void Release();

private:
    std::uint32_t m_RendererID = 0;
    Ref<Texture> m_ColorAttachment;
    Ref<Texture> m_DepthStencilAttachment;

    // Width / Height / Attachment構成を一つの設定として保持します。
    // Resize時はWidth / Heightだけを更新し、Attachment構成は維持します。
    FramebufferSpecification m_Specification;
};

} // namespace Raven
