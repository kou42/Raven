#include "Raven/Renderer/Texture/OpenGLTexture.h"

#include <glad/glad.h>

#include <iostream>

#include "Raven/Platform/OpenGL/RHI/OpenGLRHITexture.h"

namespace Raven
{

namespace
{

RHITextureFormat ToRHITextureFormat(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return RHITextureFormat::R8;
    case TextureFormat::RGB8:
        return RHITextureFormat::RGB8;
    case TextureFormat::RGBA8:
        return RHITextureFormat::RGBA8;
    case TextureFormat::R32I:
        return RHITextureFormat::R32I;
    case TextureFormat::Depth24Stencil8:
        return RHITextureFormat::Depth24Stencil8;
    case TextureFormat::None:
    default:
        return RHITextureFormat::None;
    }
}

RHITextureUsage ToRHITextureUsage(TextureUsage usage)
{
    switch (usage)
    {
    case TextureUsage::Sampled:
        return RHITextureUsage::Sampled;
    case TextureUsage::RenderTarget:
        return RHITextureUsage::RenderTarget;
    case TextureUsage::DepthStencil:
        return RHITextureUsage::DepthStencil;
    default:
        return RHITextureUsage::None;
    }
}

} // namespace

OpenGLTexture::OpenGLTexture(const TextureSpecification& specification)
    : m_Specification(specification)
{
    const RHITextureSpecification rhiSpecification = ToRHISpecification(specification);
    if (rhiSpecification.Format == RHITextureFormat::None
        || rhiSpecification.Usage == RHITextureUsage::None)
    {
        std::cerr << "OpenGLTexture creation failed. Unsupported Texture specification." << std::endl;
        return;
    }

    // 既存Texture APIの寿命は維持しつつ、GPU Resourceの所有権をRHI側へ移します。
    // Framebuffer / ImGuiが必要とするnative handleはGetID()でBridgeから取得します。
    m_RHITexture = CreateRef<OpenGLRHITexture>(rhiSpecification);
}

void OpenGLTexture::SetData(const void* data, std::size_t dataSize)
{
    if (m_RHITexture == nullptr)
    {
        std::cerr << "OpenGLTexture::SetData failed. RHI texture is not initialized." << std::endl;
        return;
    }

    m_RHITexture->SetData(data, dataSize);
}

void OpenGLTexture::Bind(unsigned int slot) const
{
    const auto openGLTexture = std::dynamic_pointer_cast<OpenGLRHITexture>(m_RHITexture);
    if (openGLTexture == nullptr)
    {
        return;
    }

    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, openGLTexture->GetRendererID());
}

void OpenGLTexture::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int OpenGLTexture::GetID() const
{
    const auto openGLTexture = std::dynamic_pointer_cast<OpenGLRHITexture>(m_RHITexture);
    if (openGLTexture == nullptr)
    {
        return 0;
    }

    return openGLTexture->GetRendererID();
}

int OpenGLTexture::GetWidth() const
{
    return static_cast<int>(m_Specification.Width);
}

int OpenGLTexture::GetHeight() const
{
    return static_cast<int>(m_Specification.Height);
}

const TextureSpecification& OpenGLTexture::GetSpecification() const
{
    return m_Specification;
}

RHITextureSpecification OpenGLTexture::ToRHISpecification(const TextureSpecification& specification)
{
    RHITextureSpecification rhiSpecification{};
    rhiSpecification.Width = specification.Width;
    rhiSpecification.Height = specification.Height;
    rhiSpecification.Format = ToRHITextureFormat(specification.Format);
    rhiSpecification.Usage = ToRHITextureUsage(specification.Usage);
    rhiSpecification.GenerateMips = specification.GenerateMips;
    rhiSpecification.DebugName = "Legacy OpenGL Texture Bridge";
    return rhiSpecification;
}

}