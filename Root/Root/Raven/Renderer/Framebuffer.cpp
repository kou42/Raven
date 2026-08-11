#include "Raven/Renderer/Framebuffer.h"

#include <glad/glad.h>

#include <cassert>

namespace Raven
{

Framebuffer::Framebuffer(std::uint32_t width, std::uint32_t height)
    : m_Width(width > 0 ? width : 1),
      m_Height(height > 0 ? height : 1)
{
    Invalidate();
}

Framebuffer::~Framebuffer()
{
    Release();
}

void Framebuffer::Bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    // Framebufferの大きさとOpenGL Viewportは別状態です。
    // Editor WindowのサイズへFramebufferを追従させてもViewportを更新しなければ
    // 以前の描画領域が使われるため、Bind時に必ず描画範囲も一致させます。
    glViewport(0, 0, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height));
}

void Framebuffer::Unbind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Resize(std::uint32_t width, std::uint32_t height)
{
    // ImGui Windowを極端に縮めた瞬間はContentRegionAvailが0になる場合があります。
    // OpenGLへ0x0 Textureを渡さず、次に有効なサイズが来るまで現在のAttachmentを維持します。
    if (width == 0 || height == 0)
    {
        return;
    }

    if (width == m_Width && height == m_Height)
    {
        return;
    }

    m_Width = width;
    m_Height = height;
    Invalidate();
}

void Framebuffer::Invalidate()
{
    Release();

    glGenFramebuffers(1, &m_RendererID);
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    // ========================================================================
    // Color Attachment
    // ========================================================================
    // RGBA8 Textureとして確保します。
    // TextureにしておくことでRendererの描画結果をコピーせずImGui::Image()から参照できます。
    glGenTextures(1, &m_ColorAttachment);
    glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        static_cast<GLsizei>(m_Width),
        static_cast<GLsizei>(m_Height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        m_ColorAttachment,
        0);

    // ========================================================================
    // Depth / Stencil Attachment
    // ========================================================================
    // Scene描画ではDepth Testを使うためColor Textureだけでは不十分です。
    // 現時点でEditorからDepth値を読む用途はないためRenderbufferとして保持します。
    glGenRenderbuffers(1, &m_DepthStencilAttachment);
    glBindRenderbuffer(GL_RENDERBUFFER, m_DepthStencilAttachment);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        static_cast<GLsizei>(m_Width),
        static_cast<GLsizei>(m_Height));
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        m_DepthStencilAttachment);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(status == GL_FRAMEBUFFER_COMPLETE);

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Release()
{
    if (m_DepthStencilAttachment != 0)
    {
        glDeleteRenderbuffers(1, &m_DepthStencilAttachment);
        m_DepthStencilAttachment = 0;
    }

    if (m_ColorAttachment != 0)
    {
        glDeleteTextures(1, &m_ColorAttachment);
        m_ColorAttachment = 0;
    }

    if (m_RendererID != 0)
    {
        glDeleteFramebuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

} // namespace Raven
