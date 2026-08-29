#include "Raven/Renderer/Framebuffer.h"

#include "Raven/Platform/OpenGL/OpenGLFramebuffer.h"
#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/Texture/Texture.h"

#include <cassert>

namespace Raven
{

std::uint32_t Framebuffer::GetColorAttachmentRendererID() const
{
    const Ref<Texture>& colorAttachment = GetColorAttachment();
    if (colorAttachment == nullptr)
    {
        return 0;
    }

    // 互換APIはTexture抽象化への移行期間だけ利用します。
    // Framebuffer自身はRenderer固有IDを所有せず、Textureが保持するIDを一時的に橋渡しします。
    return colorAttachment->GetID();
}

std::unique_ptr<Framebuffer> Framebuffer::Create(std::uint32_t width, std::uint32_t height)
{
    // ========================================================================
    // Renderer API factory
    // ========================================================================
    // Editor等の上位層は具体的なOpenGLFramebufferを知りません。
    // RendererAPI::GetAPI()を見て、このRenderer層だけでPlatform実装を選択します。
    //
    // DirectX / Vulkan対応時は各Platform実装を追加し、このswitchへ生成処理を足します。
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return std::make_unique<OpenGLFramebuffer>(width, height);

    case RendererAPI::API::DirectX11:
    case RendererAPI::API::DirectX12:
    case RendererAPI::API::Vulkan:
        // API enum自体は既に存在しますが、Framebuffer実装はまだありません。
        // 未実装APIでOpenGL実装へ暗黙fallbackするとPlatform依存のバグを隠してしまうため、
        // 明示的にassertして対応漏れを検出します。
        assert(false && "Framebuffer implementation is not available for the selected RendererAPI.");
        return nullptr;

    case RendererAPI::API::None:
    default:
        assert(false && "RendererAPI::None cannot create a Framebuffer.");
        return nullptr;
    }
}

} // namespace Raven
