#include "Raven/Platform/OpenGL/OpenGLFramebuffer.h"

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

    // ========================================================================
    // Viewport synchronization
    // ========================================================================
    // FBOのAttachmentサイズとOpenGL Viewportは別々のStateです。
    // TextureをResizeしただけでは描画領域は変わらないため、Bind時に必ずFramebufferの
    // Width/HeightへViewportを合わせます。
    glViewport(
        0,
        0,
        static_cast<GLsizei>(m_Width),
        static_cast<GLsizei>(m_Height));
}

void OpenGLFramebuffer::Unbind() const
{
    // Renderer ID 0はOpenGLのdefault framebufferです。
    // Scene/Game Viewへのoff-screen描画後、ImGui等の後続描画がEditor Windowへ戻るよう解除します。
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFramebuffer::Resize(std::uint32_t width, std::uint32_t height)
{
    // ImGui Windowを最小化した瞬間などはContentRegionが0になる場合があります。
    // OpenGLへ0x0 Textureを作らせず、次に有効なサイズが来るまで現在のAttachmentを維持します。
    if (width == 0 || height == 0)
    {
        return;
    }

    // 毎frameResize()は呼ばれますが、Windowサイズが変わっていなければGPU Resourceを
    // 再生成する必要はありません。Texture/Renderbuffer再確保は比較的重い処理なので省略します。
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
    // Sceneの描画結果をRGBA8 Textureとして保持します。
    // RenderbufferではなくTextureにしている理由は、描画完了後にコピーせず
    // Dear ImGuiのScene View / Game Viewから直接samplingして表示するためです。
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

    // Editor ViewportはTextureをほぼ1:1で表示しますが、Window Resize中にはTextureサイズと
    // 表示サイズが一時的に異なるためLinear filteringを使用します。
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
    // 3D Scene描画ではDepth Testが必要なのでColor Attachmentだけでは不十分です。
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

    // Color + Depth/Stencil Attachmentの組み合わせが描画可能か必ず検証します。
    // Attachment Formatやサイズに不整合がある場合、ここで早期に検出できます。
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(status == GL_FRAMEBUFFER_COMPLETE);

    // Resource生成処理の副作用を最小化するため、一時的にBindしたStateを解除します。
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFramebuffer::Release()
{
    // ========================================================================
    // OpenGL resource release
    // ========================================================================
    // 各IDは生成前/解放後に0を保持します。
    // これによりconstructor途中やResize、destructorなど複数の経路からRelease()を呼んでも
    // 同じResourceを二重削除しません。
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
