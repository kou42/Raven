#pragma once

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

// Source PathやPNG/JPEG等の形式を知らず、Import済みTextureAssetだけを表示するImage Widgetです。
// Assetのロード責務をWidgetから分離することで、Editor/Game UIのどちらでも同じRuntime Assetを再利用できます。
class UIImage final : public UIElement
{
public:
    void SetTexture(const Ref<TextureAsset>& texture);
    const Ref<TextureAsset>& GetTexture() const;

    void SetTintColor(const math::Vec4& color);
    const math::Vec4& GetTintColor() const;

    void SetUV(const math::Vec2& min, const math::Vec2& max);

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    Ref<TextureAsset> m_Texture;
    math::Vec4 m_TintColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec2 m_UVMin{ 0.0f, 0.0f };
    math::Vec2 m_UVMax{ 1.0f, 1.0f };
};

} // namespace Raven
