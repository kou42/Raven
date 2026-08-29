#include "Raven/Platform/OpenGL/OpenGLFramebuffer.h"

#include "Raven/Renderer/Texture/Texture.h"

#include <glad/glad.h>

#include <cassert>

namespace Raven
{

OpenGLFramebuffer::OpenGLFramebuffer(std::uint32_t width, std::uint32_t height)
    : m_Width(width > 0 ? width : 1),
      m_Height(height > 0 ? height : 1)
{
    // Constructorではサイズだけを保持するのではなく、直ちに利用可能なFBOを作ります。
    // 0サイズが渡された場合はOpenGLへ不正な0x0 Textureを渡さないよう1x1へ補正します。
    Invalidate();
}

OpenGLFramebuffer::~OpenGLFramebuffer()
{
    // OpenGL ResourceはRAIIで所有します。
    // Attachment TextureはTexture派生クラス自身がGPU Resourceを解放します。
    Release();
}

void OpenGLFramebuffer::Bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    // FBOのAttachmentサイズとOpenGL Viewportは別々のStateなので、Bind時に同期します。
    glViewport(
        0,
        0,
        static_cast<GLsizei>(m_Width),
        static_cast<GLsizei>(m_Height));
}

void OpenGLFramebuffer::Unbind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFramebuffer::Resize(std::uint32_t width, std::uint32_t height)
{
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

void OpenGLFramebuffer::Invalidate()
{
    // Resize時は古いAttachmentとFBOを先に破棄し、新しいサイズで一式を再生成します。
    Release();

    // ========================================================================
    // Framebuffer Object
    // ========================================================================
    glGenFramebuffers(1, &m_RendererID);
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    // ========================================================================
    // Color Attachment
    // ========================================================================
    // Framebuffer自身はOpenGL Textureを直接生成せず、Renderer共通のTextureSpecificationから
    // RenderTarget用途のTextureを生成します。Sampling用Textureとの差はTexture実装側が判断します。
    TextureSpecification colorSpecification;
    colorSpecification.Width = m_Width;
    colorSpecification.Height = m_Height;
    colorSpecification.Format = TextureFormat::RGBA8;
    colorSpecification.Usage = TextureUsage::RenderTarget;
    colorSpecification.GenerateMips = false;

    m_ColorAttachment = Texture::Create(colorSpecification);

    if (m_ColorAttachment == nullptr)
    {
        assert(false && "Failed to create framebuffer color attachment texture.");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        m_ColorAttachment->GetID(),
        0);

    // ========================================================================
    // Depth / Stencil Attachment
    // ========================================================================
    // 以前はOpenGLFramebufferがRenderbufferを直接生成していましたが、Colorと同様に
    // Texture抽象化へ所有権を移します。将来Depth sampling、Picking、Shadow等が必要になっても
    // 同じTextureインターフェースを基礎として拡張できます。
    TextureSpecification depthStencilSpecification;
    depthStencilSpecification.Width = m_Width;
    depthStencilSpecification.Height = m_Height;
    depthStencilSpecification.Format = TextureFormat::Depth24Stencil8;
    depthStencilSpecification.Usage = TextureUsage::DepthStencil;
    depthStencilSpecification.GenerateMips = false;

    m_DepthStencilAttachment = Texture::Create(depthStencilSpecification);

    if (m_DepthStencilAttachment == nullptr)
    {
        assert(false && "Failed to create framebuffer depth/stencil attachment texture.");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_TEXTURE_2D,
        m_DepthStencilAttachment->GetID(),
        0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(status == GL_FRAMEBUFFER_COMPLETE);

    // Resource生成処理で変更したbindingを解除し、後続のRenderer処理へ不要なStateを残しません。
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFramebuffer::Release()
{
    // Color / DepthStencil AttachmentともTexture派生クラスのRAIIへGPU Resource解放を委譲します。
    if (m_DepthStencilAttachment != nullptr)
    {
        m_DepthStencilAttachment.reset();
    }

    if (m_ColorAttachment != nullptr)
    {
        m_ColorAttachment.reset();
    }

    // Framebuffer Object自体だけはOpenGLFramebufferの責務なので、ここで明示的に解放します。
    if (m_RendererID != 0)
    {
        glDeleteFramebuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

} // namespace Raven
