#include "Raven/Renderer/Framebuffer.h"

#include "Raven/Platform/OpenGL/OpenGLFramebuffer.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/Texture/Texture.h"

#include <cassert>

namespace Raven
{

std::uint32_t Framebuffer::GetColorAttachmentRendererID() const
{
    // 従来の単一Color Attachment利用側はColor 0を参照します。
    return GetColorAttachmentRendererID(0);
}

std::uint32_t Framebuffer::GetColorAttachmentRendererID(std::size_t index) const
{
    const Ref<Texture>& colorAttachment = GetColorAttachment(index);
    if (colorAttachment == nullptr)
    {
        return 0;
    }

    // 互換APIはTexture抽象化への移行期間だけ利用します。
    // Framebuffer自身はRenderer固有IDを所有せず、指定されたTextureが保持するIDを一時的に橋渡しします。
    return colorAttachment->GetID();
}

std::unique_ptr<Framebuffer> Framebuffer::Create(std::uint32_t width, std::uint32_t height)
{
    // ========================================================================
    // Legacy-compatible factory
    // ========================================================================
    // 既存のScene/Game Viewコードを変更せず利用できるよう、従来のwidth/height APIは残します。
    // 実際の生成経路はSpecification版へ一本化し、Attachment構成の重複実装を避けます。
    FramebufferSpecification specification;
    specification.Width = width;
    specification.Height = height;

    return Create(specification);
}

std::unique_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& specification)
{
    // ========================================================================
    // RHI Backend factory
    // ========================================================================
    // Editor等の上位層は具体的なOpenGLFramebufferを知りません。
    // RHI Backend識別子だけを見て、このRenderer層でPlatform実装を選択します。
    //
    // DirectX / Vulkan対応時は各Platform実装を追加し、このswitchへ生成処理を足します。
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return std::make_unique<OpenGLFramebuffer>(specification);

    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
        // Backend enum自体は既に存在しますが、Framebuffer実装はまだありません。
        // 未実装BackendでOpenGL実装へ暗黙fallbackするとPlatform依存のバグを隠してしまうため、
        // 明示的にassertして対応漏れを検出します。
        assert(false && "Framebuffer implementation is not available for the selected RHI backend.");
        return nullptr;

    case RHIBackend::None:
    default:
        assert(false && "RHIBackend::None cannot create a Framebuffer.");
        return nullptr;
    }
}

} // namespace Raven
