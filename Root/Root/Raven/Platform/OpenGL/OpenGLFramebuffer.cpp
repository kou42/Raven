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
    // destructorからFBOを安全に解放でき、Attachment TextureはTexture派生クラス自身が
    // GPU ResourceをRAIIで解放します。
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

    // 毎frame Resize()は呼ばれますが、Windowサイズが変わっていなければGPU Resourceを
    // 再生成する必要はありません。Texture再確保は比較的重い処理なので省略します。
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
    // EditorのScene View / Game Viewなどから直接samplingして表示できるようにするためです。
    //
    // 以前はFramebuffer自身がglGenTextures / glTexImage2D / glDeleteTexturesを直接管理していました。
    // 現在はRenderer共通のTextureSpecificationからRenderTarget用途のTextureを生成し、
    // Textureの生成・寿命管理をTexture抽象クラスへ委譲しています。
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
    // 3D Scene描画ではDepth Testが必要なのでColor Attachmentだけでは不十分です。
    // 以前はOpenGLFramebufferがGL_DEPTH24_STENCIL8 Renderbufferを直接生成していましたが、
    // 現在はColorと同様にDepth24Stencil8 TextureとしてTexture抽象化へ所有権を移しています。
    // 将来Depth sampling、Picking、Shadow等が必要になっても同じTexture基盤を利用できます。
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

    // Color + Depth/Stencil Attachmentの組み合わせが描画可能か必ず検証します。
    // Attachment Formatやサイズに不整合がある場合、ここで早期に検出できます。
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    assert(status == GL_FRAMEBUFFER_COMPLETE);

    // Resource生成処理の副作用を最小化するため、一時的に変更したbindingを解除します。
    // Textureの生成過程でもGL_TEXTURE_2DがBindされるため、後続のRenderer処理へ不要なStateを残しません。
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFramebuffer::Release()
{
    // ========================================================================
    // OpenGL resource release
    // ========================================================================
    // AttachmentはRef<Texture>として所有し、Texture派生クラスのRAIIへGPU Texture解放を委譲します。
    // FBO IDは0を「未生成」として扱うため、Resize / destructorなど複数の経路からRelease()を
    // 呼び出しても同じOpenGL Resourceを二重削除しません。
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
