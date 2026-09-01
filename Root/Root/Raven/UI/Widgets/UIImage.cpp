#include "Raven/UI/Widgets/UIImage.h"

namespace Raven
{

void UIImage::SetTexture(const Ref<TextureAsset>& texture)
{
    m_Texture = texture;
}

const Ref<TextureAsset>& UIImage::GetTexture() const
{
    return m_Texture;
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
    if (m_Texture == nullptr || m_Texture->IsValid() == false)
    {
        return;
    }

    const math::Vec2& size = GetSize();
    drawList.AddImage(
        absolutePosition,
        math::Vec2{ absolutePosition.x + size.x, absolutePosition.y + size.y },
        m_Texture,
        m_TintColor,
        m_UVMin,
        m_UVMax);
}

} // namespace Raven
