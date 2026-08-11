#pragma once

#include "Raven/Renderer/Framebuffer.h"

#include <cstdint>

namespace Raven
{

// ============================================================================
// OpenGLFramebuffer
// ============================================================================
// Framebuffer抽象インターフェースのOpenGL実装です。
// OpenGL固有のFBO / Texture / Renderbuffer IDはこのクラスだけが所有し、
// Editor等の上位層へOpenGL APIやGLuintを直接露出させないことを目的とします。
//
// Color Attachment:
//   RGBA8の2D Textureとして保持します。
//   Scene/Game ViewではこのTextureをDear ImGuiから参照して表示します。
//
// Depth/Stencil Attachment:
//   現段階ではEditor側からDepth Textureをsampling/readbackする用途がないため、
//   GL_DEPTH24_STENCIL8 Renderbufferとして保持します。
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

    std::uint32_t GetColorAttachmentRendererID() const override { return m_ColorAttachment; }
    std::uint32_t GetWidth() const override { return m_Width; }
    std::uint32_t GetHeight() const override { return m_Height; }

private:
    // 現在のWidth/Heightを使ってOpenGL Resource一式を再生成します。
    // Resize時にも同じ処理を使うため、生成処理を一箇所へ集約します。
    void Invalidate();

    // 所有しているOpenGL Resourceを安全に解放します。
    // 0は「未生成」として扱い、複数回呼ばれても問題ない実装にします。
    void Release();

private:
    std::uint32_t m_RendererID = 0;
    std::uint32_t m_ColorAttachment = 0;
    std::uint32_t m_DepthStencilAttachment = 0;

    std::uint32_t m_Width = 1;
    std::uint32_t m_Height = 1;
};

} // namespace Raven
