#pragma once

#include <cstdint>

namespace Raven
{

// ============================================================================
// Framebuffer
// ============================================================================
// EditorのScene View / Game Viewなど、Main Windowとは別の描画先を作るための
// OpenGL Framebufferを所有する小さなRAIIクラスです。
//
// Color Attachmentは通常の2D Textureとして作成するため、描画完了後に
// ImGui::Image()へTexture IDを渡してそのままEditor Window内へ表示できます。
// Depth/Stencilは現段階ではTextureとして参照する必要がないためRenderbufferを使います。
class Framebuffer
{
public:
    Framebuffer(std::uint32_t width, std::uint32_t height);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void Bind() const;
    void Unbind() const;

    // Viewport Windowのリサイズへ追従します。
    // 同じサイズの場合はGPU Resourceを作り直さないため何もしません。
    void Resize(std::uint32_t width, std::uint32_t height);

    std::uint32_t GetColorAttachmentRendererID() const { return m_ColorAttachment; }
    std::uint32_t GetWidth() const { return m_Width; }
    std::uint32_t GetHeight() const { return m_Height; }

private:
    void Invalidate();
    void Release();

private:
    std::uint32_t m_RendererID = 0;
    std::uint32_t m_ColorAttachment = 0;
    std::uint32_t m_DepthStencilAttachment = 0;
    std::uint32_t m_Width = 1;
    std::uint32_t m_Height = 1;
};

} // namespace Raven
