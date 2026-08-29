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
    // EditorLayer::OnDetach()がOpenGL Context生存中にFramebufferを破棄するため、
    // destructorからglDelete*を安全に呼び出せます。
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
    // 以前はFramebuffer自身がglGenTextures / glTexImage2D / glDeleteTexturesを直接管理していました。
    // 現在はTexture抽象クラスへ生成と寿命管理を委譲し、Framebufferは「TextureをAttachmentする」
    // 責務だけを持ちます。これによりTexture生成仕様をRenderer全体で共通化できます。
    TextureSpecification colorSpecification;
    colorSpecification.Width = m_Width;
    colorSpecification.Height = m_Height;
    colorSpecification.Format = TextureFormat::RGBA8;
    colorSpecification.GenerateMips = false;

    m_ColorAttachment = Texture::Create(colorSpecification);

    if (m_ColorAttachment == nullptr)
    {
        assert(false && "Failed to create framebuffer color attachment texture.");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    // OpenGLFramebufferはOpenGL backend内部なので、FramebufferへTextureをAttachmentするために
    // 現在はTextureのRenderer IDを利用します。上位層にはこのIDを公開しません。
    // TextureのNative Handle抽象化を追加する段階で、この境界もさらに整理できます。
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        m_ColorAttachment->GetID(),
        0);

    // ========================================================================
    // Depth / Stencil Attachment
    // ========================================================================
    // 現段階ではDepth値をShaderやEditor Pickingから参照しないため、Textureではなく
    // RenderbufferとしてGL_DEPTH24_STENCIL8を確保します。
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

void OpenGLFramebuffer::Release()
{
    // Depth/StencilとFBOはOpenGLFramebuffer自身が所有します。
    if (m_DepthStencilAttachment != 0)
    {
        glDeleteRenderbuffers(1, &m_DepthStencilAttachment);
        m_DepthStencilAttachment = 0;
    }

    // Color AttachmentのGPU Texture ResourceはTexture派生クラス自身がRAIIで解放します。
    // FramebufferからglDeleteTexturesを直接呼ばないことでTexture所有権を一箇所に集約します。
    if (m_ColorAttachment != nullptr)
    {
        m_ColorAttachment.reset();
    }

    if (m_RendererID != 0)
    {
        glDeleteFramebuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

} // namespace Raven
