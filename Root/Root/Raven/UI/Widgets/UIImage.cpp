#include "Raven/UI/Widgets/UIImage.h"

#include <algorithm>

namespace Raven
{

UIImage::UIImage()
{
    // Animation Transform等でImage QuadがBoundsを越えてもWidget外へ描画しません。
    SetClipChildren(true);
    SetClipSelf(true);
}

void UIImage::SetTexture(const Ref<TextureAsset>& texture)
{
    m_TextureView = UITextureView::FromAsset(texture);
}

const Ref<TextureAsset>& UIImage::GetTexture() const
{
    return m_TextureView.GetAsset();
}

void UIImage::SetTextureView(const UITextureView& textureView)
{
    m_TextureView = textureView;
}

const UITextureView& UIImage::GetTextureView() const
{
    return m_TextureView;
}

void UIImage::SetRenderTargetTexture(
    const Ref<Texture>& texture,
    UITextureOrigin origin)
{
    m_TextureView = UITextureView::FromTexture(texture, origin);
}

void UIImage::SetRHIRenderTargetTexture(
    const Ref<RHITexture>& texture,
    UITextureOrigin origin)
{
    m_TextureView = UITextureView::FromRHITexture(texture, origin);
}

void UIImage::SetScaleMode(UIImageScaleMode scaleMode)
{
    m_ScaleMode = scaleMode;
}

UIImageScaleMode UIImage::GetScaleMode() const
{
    return m_ScaleMode;
}

UIRect UIImage::GetImageRect() const
{
    UIRect result;
    result.Max = GetSize();
    if (m_ScaleMode != UIImageScaleMode::AspectFit ||
        m_TextureView.IsValid() == false ||
        result.Max.x <= 0.0f || result.Max.y <= 0.0f)
    {
        return result;
    }

    const float sourceWidth = static_cast<float>(m_TextureView.GetWidth());
    const float sourceHeight = static_cast<float>(m_TextureView.GetHeight());
    const float scale = std::min(result.Max.x / sourceWidth, result.Max.y / sourceHeight);
    const math::Vec2 imageSize(sourceWidth * scale, sourceHeight * scale);
    result.Min = math::Vec2(
        (result.Max.x - imageSize.x) * 0.5f,
        (result.Max.y - imageSize.y) * 0.5f);
    result.Max = result.Min + imageSize;
    return result;
}

bool UIImage::TryMapLocalPositionToTexturePixel(
    const math::Vec2& localPosition,
    std::uint32_t& outX,
    std::uint32_t& outY) const
{
    const UIRect imageRect = GetImageRect();
    const float width = imageRect.Max.x - imageRect.Min.x;
    const float height = imageRect.Max.y - imageRect.Min.y;
    if (m_TextureView.IsValid() == false || width <= 0.0f || height <= 0.0f ||
        localPosition.x < imageRect.Min.x || localPosition.y < imageRect.Min.y ||
        localPosition.x >= imageRect.Max.x || localPosition.y >= imageRect.Max.y)
    {
        return false;
    }

    const math::Vec2 normalized(
        (localPosition.x - imageRect.Min.x) / width,
        (localPosition.y - imageRect.Min.y) / height);
    const math::Vec2 logicalUV(
        m_UVMin.x + (m_UVMax.x - m_UVMin.x) * normalized.x,
        m_UVMin.y + (m_UVMax.y - m_UVMin.y) * normalized.y);
    return m_TextureView.TryMapLogicalUVToPixel(logicalUV, outX, outY);
}

bool UIImage::TryMapScreenPositionToTexturePixel(
    const math::Vec2& screenPosition,
    std::uint32_t& outX,
    std::uint32_t& outY) const
{
    math::Vec2 localPosition;
    if (TryScreenToLocalPosition(screenPosition, localPosition) == false)
    {
        return false;
    }
    return TryMapLocalPositionToTexturePixel(localPosition, outX, outY);
}

void UIImage::SetTintColor(const math::Vec4& color)
{
    m_TintColor = color;
}

const math::Vec4& UIImage::GetTintColor() const
{
    return m_TintColor;
}

void UIImage::SetUV(const math::Vec2& min, const math::Vec2& max)
{
    m_UVMin = min;
    m_UVMax = max;
}

void UIImage::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
{
    if (m_TextureView.IsValid() == false)
    {
        return;
    }

    const UIRect imageRect = GetImageRect();
    drawList.AddImage(
        absolutePosition + imageRect.Min,
        absolutePosition + imageRect.Max,
        m_TextureView,
        ApplyVisualColor(m_TintColor),
        m_UVMin,
        m_UVMax);
}

} // namespace Raven
