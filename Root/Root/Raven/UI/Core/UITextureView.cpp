#include "Raven/UI/Core/UITextureView.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/RHI/RHITexture.h"
#include "Raven/Renderer/Texture/Texture.h"

#include <algorithm>
#include <cmath>

namespace Raven
{

UITextureView UITextureView::FromAsset(
    const Ref<TextureAsset>& asset,
    UITextureOrigin origin)
{
    UITextureView view;
    view.m_SourceType = UITextureSourceType::Asset;
    view.m_Origin = origin;
    view.m_Asset = asset;
    return view;
}

UITextureView UITextureView::FromTexture(
    const Ref<Texture>& texture,
    UITextureOrigin origin)
{
    UITextureView view;
    view.m_SourceType = UITextureSourceType::LegacyTexture;
    view.m_Origin = origin;
    view.m_LegacyTexture = texture;
    return view;
}

UITextureView UITextureView::FromRHITexture(
    const Ref<RHITexture>& texture,
    UITextureOrigin origin)
{
    UITextureView view;
    view.m_SourceType = UITextureSourceType::RHITexture;
    view.m_Origin = origin;
    view.m_RHITexture = texture;
    return view;
}

bool UITextureView::IsValid() const
{
    if (m_SourceType == UITextureSourceType::Asset)
    {
        return m_Asset != nullptr && m_Asset->IsValid() == true &&
            GetWidth() > 0u && GetHeight() > 0u;
    }
    if (m_SourceType == UITextureSourceType::LegacyTexture)
    {
        return m_LegacyTexture != nullptr && m_LegacyTexture->GetID() != 0u &&
            GetWidth() > 0u && GetHeight() > 0u;
    }
    if (m_SourceType == UITextureSourceType::RHITexture)
    {
        return m_RHITexture != nullptr &&
            m_RHITexture->GetSpecification().Format != RHITextureFormat::None &&
            GetWidth() > 0u && GetHeight() > 0u;
    }
    return false;
}

UITextureSourceType UITextureView::GetSourceType() const
{
    return m_SourceType;
}

UITextureOrigin UITextureView::GetOrigin() const
{
    return m_Origin;
}

std::uint32_t UITextureView::GetWidth() const
{
    if (m_SourceType == UITextureSourceType::Asset && m_Asset != nullptr)
    {
        const Ref<Texture>& texture = m_Asset->GetTexture();
        if (texture != nullptr && texture->GetWidth() > 0)
        {
            return static_cast<std::uint32_t>(texture->GetWidth());
        }
        return m_Asset->HasPixelData() == true ? m_Asset->GetPixelData().Width : 0u;
    }
    if (m_SourceType == UITextureSourceType::LegacyTexture &&
        m_LegacyTexture != nullptr && m_LegacyTexture->GetWidth() > 0)
    {
        return static_cast<std::uint32_t>(m_LegacyTexture->GetWidth());
    }
    if (m_SourceType == UITextureSourceType::RHITexture && m_RHITexture != nullptr)
    {
        return m_RHITexture->GetSpecification().Width;
    }
    return 0u;
}

std::uint32_t UITextureView::GetHeight() const
{
    if (m_SourceType == UITextureSourceType::Asset && m_Asset != nullptr)
    {
        const Ref<Texture>& texture = m_Asset->GetTexture();
        if (texture != nullptr && texture->GetHeight() > 0)
        {
            return static_cast<std::uint32_t>(texture->GetHeight());
        }
        return m_Asset->HasPixelData() == true ? m_Asset->GetPixelData().Height : 0u;
    }
    if (m_SourceType == UITextureSourceType::LegacyTexture &&
        m_LegacyTexture != nullptr && m_LegacyTexture->GetHeight() > 0)
    {
        return static_cast<std::uint32_t>(m_LegacyTexture->GetHeight());
    }
    if (m_SourceType == UITextureSourceType::RHITexture && m_RHITexture != nullptr)
    {
        return m_RHITexture->GetSpecification().Height;
    }
    return 0u;
}

const Ref<TextureAsset>& UITextureView::GetAsset() const
{
    return m_Asset;
}

const Ref<Texture>& UITextureView::GetLegacyTexture() const
{
    return m_LegacyTexture;
}

const Ref<RHITexture>& UITextureView::GetRHITexture() const
{
    return m_RHITexture;
}

math::Vec2 UITextureView::ResolveResourceUV(const math::Vec2& logicalUV) const
{
    if (m_Origin == UITextureOrigin::BottomLeft)
    {
        return math::Vec2(logicalUV.x, 1.0f - logicalUV.y);
    }
    return logicalUV;
}

bool UITextureView::TryMapLogicalUVToPixel(
    const math::Vec2& logicalUV,
    std::uint32_t& outX,
    std::uint32_t& outY) const
{
    const std::uint32_t width = GetWidth();
    const std::uint32_t height = GetHeight();
    if (width == 0u || height == 0u ||
        std::isfinite(logicalUV.x) == false || std::isfinite(logicalUV.y) == false ||
        logicalUV.x < 0.0f || logicalUV.y < 0.0f ||
        logicalUV.x > 1.0f || logicalUV.y > 1.0f)
    {
        return false;
    }

    outX = std::min(
        static_cast<std::uint32_t>(logicalUV.x * static_cast<float>(width)),
        width - 1u);
    const float resourceV = m_Origin == UITextureOrigin::BottomLeft
        ? 1.0f - logicalUV.y : logicalUV.y;
    // BottomLeftのlogical y=0は最上段なので、境界値1.0を最後のPixelへClampします。
    outY = std::min(
        static_cast<std::uint32_t>(resourceV * static_cast<float>(height)),
        height - 1u);
    return true;
}

} // namespace Raven
