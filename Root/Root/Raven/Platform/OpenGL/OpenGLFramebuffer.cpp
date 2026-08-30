#include "Raven/Platform/OpenGL/OpenGLFramebuffer.h"

#include "Raven/Renderer/Texture/Texture.h"

#include <glad/glad.h>

#include <cassert>

namespace Raven
{

namespace
{

FramebufferSpecification CreateDefaultFramebufferSpecification(
    std::uint32_t width,
    std::uint32_t height)
{
    FramebufferSpecification specification;
    specification.Width = width;
    specification.Height = height;
    return specification;
}

bool IsDepthStencilFormat(TextureFormat format)
{
    return format == TextureFormat::Depth24Stencil8;
}

} // namespace

OpenGLFramebuffer::OpenGLFramebuffer(std::uint32_t width, std::uint32_t height)
    : OpenGLFramebuffer(CreateDefaultFramebufferSpecification(width, height))
{
}

OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& specification)
    : m_Specification(specification)
{
    // ConstructorではSpecificationを保持するだけでなく、直ちに利用可能なFBOを作ります。
    // 0サイズが渡された場合はOpenGLへ不正な0x0 Textureを渡さないよう1x1へ補正します。
    if (m_Specification.Width == 0)
    {
        m_Specification.Width = 1;
    }

    if (m_Specification.Height == 0)
    {
        m_Specification.Height = 1;
    }

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
        static_cast<GLsizei>(m_Specification.Width),
        static_cast<GLsizei>(m_Specification.Height));
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
    if (width == m_Specification.Width && height == m_Specification.Height)
    {
        return;
    }

    // Attachment構成は維持し、サイズだけを更新して同じ用途のFramebufferを再生成します。
    m_Specification.Width = width;
    m_Specification.Height = height;
    Invalidate();
}

void OpenGLFramebuffer::Invalidate()
{
    // Resize時は古いAttachmentとFBOを先に破棄し、新しいSpecificationで一式を再生成します。
    Release();

    // ========================================================================
    // Framebuffer Object
    // ========================================================================
    glGenFramebuffers(1, &m_RendererID);
    glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

    bool hasColorAttachment = false;
    bool hasDepthStencilAttachment = false;

    // ========================================================================
    // Attachment creation
    // ========================================================================
    // FramebufferSpecificationに列挙されたFormatを順番に解釈します。
    // 現段階ではColor 1枚 + DepthStencil 1枚を扱い、将来MRTを導入する際にはColor Attachmentを
    // vector化してGL_COLOR_ATTACHMENT0 + indexへ接続する方針です。
    for (const FramebufferAttachmentSpecification& attachmentSpecification
        : m_Specification.Attachments.Attachments)
    {
        const TextureFormat format = attachmentSpecification.Format;

        if (format == TextureFormat::None)
        {
            // Noneは未指定値なのでGPU Resourceを生成しません。
            continue;
        }

        if (IsDepthStencilFormat(format))
        {
            // Depth/Stencil Attachmentは1枚だけ許可します。
            // 同一Framebufferへ複数Depthを指定してもOpenGLの同じAttachment pointを共有できないため、
            // 設定ミスとして早期に検出します。
            if (hasDepthStencilAttachment)
            {
                assert(false && "Framebuffer supports only one depth/stencil attachment.");
                continue;
            }

            TextureSpecification depthStencilSpecification;
            depthStencilSpecification.Width = m_Specification.Width;
            depthStencilSpecification.Height = m_Specification.Height;
            depthStencilSpecification.Format = format;
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

            hasDepthStencilAttachment = true;
            continue;
        }

        // ====================================================================
        // Color Attachment
        // ====================================================================
        // Sceneの描画結果等をTextureとして保持します。
        // RenderbufferではなくTextureにしておくことで、描画完了後にコピーせずPost Processや
        // Editor Viewportなどからsampling可能なRenderTargetとして再利用できます。
        if (hasColorAttachment)
        {
            // 現在のFramebuffer公開APIは単一Color Attachmentを前提としているため、
            // MRT対応前に複数Colorを黙って無視せず設定ミスとして検出します。
            assert(false && "Multiple color attachments are not supported yet.");
            continue;
        }

        TextureSpecification colorSpecification;
        colorSpecification.Width = m_Specification.Width;
        colorSpecification.Height = m_Specification.Height;
        colorSpecification.Format = format;
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

        hasColorAttachment = true;
    }

    // Attachmentを1つも持たないFramebufferは描画先として成立しないため設定ミスとして扱います。
    if (hasColorAttachment == false && hasDepthStencilAttachment == false)
    {
        assert(false && "FramebufferSpecification must contain at least one valid attachment.");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    // Depth-only FramebufferはShadow Map等で利用できます。
    // Color Attachmentが存在しない場合、OpenGLへColor出力先がないことを明示します。
    if (hasColorAttachment == false)
    {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

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
