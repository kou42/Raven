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
//   RGBA8 / RenderTarget用途のTexture抽象クラスとして保持します。
//
// Depth/Stencil Attachment:
//   Depth24Stencil8 / DepthStencil用途のTexture抽象クラスとして保持します。
//   これによりColor/DepthともGPU Texture Resourceの生成・破棄責務をTextureへ統一し、
//   Framebufferは「どのTextureをどのAttachmentへ接続するか」だけを担当します。
class OpenGLFramebuffer final : public Framebuffer
{
public:
    OpenGLFramebuffer(std::uint32_t width, std::uint32_t height);
    ~OpenGLFramebuffer() override;

    OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
    OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;

    void Bind() const override;
    void Unbind() const override;
    void Resize(std::uint32_t width, std::uint32_t height) override;

    const Ref<Texture>& GetColorAttachment() const override { return m_ColorAttachment; }
    std::uint32_t GetWidth() const override { return m_Width; }
    std::uint32_t GetHeight() const override { return m_Height; }

private:
    // 現在のWidth/Heightを使ってOpenGL Resource一式を再生成します。
    // Resize時にも同じ処理を使うため、生成処理を一箇所へ集約します。
    void Invalidate();

    // 所有しているOpenGL Resourceを安全に解放します。
    // Texture AttachmentはRefのreset()によりTextureクラス自身のRAIIへ解放を委譲します。
    void Release();

private:
    std::uint32_t m_RendererID = 0;
    Ref<Texture> m_ColorAttachment;
    Ref<Texture> m_DepthStencilAttachment;

    std::uint32_t m_Width = 1;
    std::uint32_t m_Height = 1;
};

} // namespace Raven
