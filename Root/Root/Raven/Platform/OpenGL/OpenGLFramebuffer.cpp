#include "Raven/Platform/OpenGL/OpenGLFramebuffer.h"

#include "Raven/Renderer/Texture/Texture.h"

#include <glad/glad.h>

#include <cassert>
#include <vector>

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

const Ref<Texture>& OpenGLFramebuffer::GetColorAttachment(std::size_t index) const
{
    // index範囲外を黙ってColor 0へfallbackすると、G-Buffer等でAttachmentの取り違えを
    // 発見しにくくなるためassertで呼び出し側の設定ミスを検出します。
    assert(index < m_ColorAttachments.size());

    if (index >= m_ColorAttachments.size())
    {
        // Release buildでassertが無効でも未定義アクセスを避けます。
        // 空Refをstaticで保持し、参照戻り値APIの安全なfallbackとして利用します。
        static const Ref<Texture> nullTexture = nullptr;
        return nullTexture;
    }

    return m_ColorAttachments[index];
}

int OpenGLFramebuffer::ReadPixel(std::size_t attachmentIndex, int x, int y) const
{
    if (attachmentIndex >= m_ColorAttachments.size())
    {
        assert(false && "Framebuffer::ReadPixel attachment index is out of range.");
        return -1;
    }

    if (x < 0 || y < 0 ||
        x >= static_cast<int>(m_Specification.Width) ||
        y >= static_cast<int>(m_Specification.Height))
    {
        // Viewport外をクリックした場合は未選択値として扱います。
        return -1;
    }

    const Ref<Texture>& attachment = m_ColorAttachments[attachmentIndex];
    if (attachment == nullptr)
    {
        return -1;
    }

    // 現段階のReadPixelはEntity ID Buffer用R32Iに限定します。
    // RGBA8等を同じint戻り値へ暗黙変換するとAPIの意味が曖昧になるため、形式を明示的に検証します。
    if (attachment->GetSpecification().Format != TextureFormat::R32I)
    {
        assert(false && "Framebuffer::ReadPixel currently supports only R32I attachments.");
        return -1;
    }

    // ReadPixelは描画中に呼ばれる可能性があるため、現在のRead Framebuffer / Read Buffer Stateを
    // 保存してから一時的にこのFramebufferへ切り替え、読み戻し後に必ず元へ戻します。
    GLint previousReadFramebuffer = 0;
    GLint previousReadBuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(attachmentIndex));

    int pixelData = -1;
    glReadPixels(
        x,
        y,
        1,
        1,
        GL_RED_INTEGER,
        GL_INT,
        &pixelData);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));

    return pixelData;
}

void OpenGLFramebuffer::ClearAttachment(std::size_t attachmentIndex, int value)
{
    if (attachmentIndex >= m_ColorAttachments.size())
    {
        assert(false && "Framebuffer::ClearAttachment attachment index is out of range.");
        return;
    }

    const Ref<Texture>& attachment = m_ColorAttachments[attachmentIndex];
    if (attachment == nullptr)
    {
        return;
    }

    // 整数ID Bufferを未選択値(-1等)へ初期化する用途としてR32Iだけを対象にします。
    // Color Attachmentの通常クリアはRendererAPI::Clear / glClearColor側と責務を分けます。
    if (attachment->GetSpecification().Format != TextureFormat::R32I)
    {
        assert(false && "Framebuffer::ClearAttachment currently supports only R32I attachments.");
        return;
    }

    GLint previousDrawFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_RendererID);

    // glClearBufferivのdrawbuffer引数はGL_COLOR_ATTACHMENTnではなく、
    // 現在のDraw Buffer配列におけるColor indexを指定します。
    // RavenではColor Attachmentを0から連続割り当てしているためvector indexをそのまま利用できます。
    glClearBufferiv(
        GL_COLOR,
        static_cast<GLint>(attachmentIndex),
        &value);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
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

    bool hasDepthStencilAttachment = false;

    // OpenGL実装が実際に対応可能なColor Attachment数をGPUから取得します。
    // 固定値で制限するとGPU能力より狭くなる一方、無制限にGL_COLOR_ATTACHMENTnを生成すると
    // 不完全Framebufferになるため、Specificationと実機上限の両方を検証します。
    GLint maxColorAttachments = 0;
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);

    // ========================================================================
    // Attachment creation
    // ========================================================================
    // FramebufferSpecificationに列挙されたFormatを順番に解釈します。
    // Color Formatはm_ColorAttachmentsへ追加した順にGL_COLOR_ATTACHMENT0 + indexへ接続し、
    // DepthStencil FormatはColor indexへ含めません。
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
        // Sceneの描画結果、Picking ID、G-Buffer等をそれぞれ独立したTextureとして保持します。
        // RenderbufferではなくTextureにしておくことで、描画完了後にコピーせずPost Processや
        // Editor Viewportなどからsampling可能なRenderTargetとして再利用できます。
        if (static_cast<GLint>(m_ColorAttachments.size()) >= maxColorAttachments)
        {
            assert(false && "Framebuffer color attachment count exceeds GL_MAX_COLOR_ATTACHMENTS.");
            continue;
        }

        TextureSpecification colorSpecification;
        colorSpecification.Width = m_Specification.Width;
        colorSpecification.Height = m_Specification.Height;
        colorSpecification.Format = format;
        colorSpecification.Usage = TextureUsage::RenderTarget;
        colorSpecification.GenerateMips = false;

        Ref<Texture> colorAttachment = Texture::Create(colorSpecification);

        if (colorAttachment == nullptr)
        {
            assert(false && "Failed to create framebuffer color attachment texture.");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }

        const std::size_t colorAttachmentIndex = m_ColorAttachments.size();
        const GLenum attachmentPoint =
            GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(colorAttachmentIndex);

        // OpenGLFramebufferはOpenGL backend内部なので、FramebufferへTextureをAttachmentするために
        // 現在はTextureのRenderer IDを利用します。上位層にはこのIDを公開しません。
        // TextureのNative Handle抽象化を追加する段階で、この境界もさらに整理できます。
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            attachmentPoint,
            GL_TEXTURE_2D,
            colorAttachment->GetID(),
            0);

        m_ColorAttachments.push_back(colorAttachment);
    }

    // Attachmentを1つも持たないFramebufferは描画先として成立しないため設定ミスとして扱います。
    if (m_ColorAttachments.empty() && hasDepthStencilAttachment == false)
    {
        assert(false && "FramebufferSpecification must contain at least one valid attachment.");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    if (m_ColorAttachments.empty())
    {
        // Depth-only FramebufferはShadow Map等で利用できます。
        // Color Attachmentが存在しない場合、OpenGLへColor出力先がないことを明示します。
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    else if (m_ColorAttachments.size() == 1)
    {
        // 単一Color Attachmentでも出力先を明示し、MRT経路とStateの意味を統一します。
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    else
    {
        // ====================================================================
        // Multiple Render Targets
        // ====================================================================
        // Fragment Shaderのlayout(location = N)出力をColor Attachment Nへ対応させます。
        // GL_COLOR_ATTACHMENT0から連続して割り当てているため、draw buffer一覧も同じ順序で生成できます。
        std::vector<GLenum> drawBuffers;
        drawBuffers.reserve(m_ColorAttachments.size());

        for (std::size_t index = 0; index < m_ColorAttachments.size(); ++index)
        {
            drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
        }

        glDrawBuffers(
            static_cast<GLsizei>(drawBuffers.size()),
            drawBuffers.data());
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

    // vectorをclearすると各Ref<Texture>の参照が解放され、他に所有者がいないAttachmentは
    // OpenGLTextureのdestructorを通してGPU Textureも解放されます。
    m_ColorAttachments.clear();

    // Framebuffer Object自体だけはOpenGLFramebufferの責務なので、ここで明示的に解放します。
    if (m_RendererID != 0)
    {
        glDeleteFramebuffers(1, &m_RendererID);
        m_RendererID = 0;
    }
}

} // namespace Raven
