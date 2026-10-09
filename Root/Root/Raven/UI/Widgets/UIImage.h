#pragma once

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Core/UITextureView.h"

#include <cstdint>

namespace Raven
{

enum class UIImageScaleMode
{
    Stretch = 0,
    AspectFit
};

// Source PathやGPU API固有handleを知らず、AssetとRenderTargetを同じ描画契約で表示するImage Widgetです。
// Resource生成責務をWidgetから分離することで、Editor/Game UIと各Renderer Backendで同じ配置処理を再利用できます。
class UIImage final : public UIElement
{
public:
    UIImage();

    void SetTexture(const Ref<TextureAsset>& texture);
    const Ref<TextureAsset>& GetTexture() const;
    void SetTextureView(const UITextureView& textureView);
    const UITextureView& GetTextureView() const;

    // Legacy Framebuffer AttachmentはOpenGLのBottomLeft生成規約を明示して渡します。
    void SetRenderTargetTexture(
        const Ref<Texture>& texture,
        UITextureOrigin origin = UITextureOrigin::BottomLeft);
    void SetRHIRenderTargetTexture(
        const Ref<RHITexture>& texture,
        UITextureOrigin origin = UITextureOrigin::TopLeft);

    void SetScaleMode(UIImageScaleMode scaleMode);
    UIImageScaleMode GetScaleMode() const;
    UIRect GetImageRect() const;

    // localPositionはUIImage左上を(0,0)とするUI論理座標です。
    // AspectFitのLetterbox外はfalseとし、Pickingを映像外へ伝播させません。
    bool TryMapLocalPositionToTexturePixel(
        const math::Vec2& localPosition,
        std::uint32_t& outX,
        std::uint32_t& outY) const;

    // Window/UIContextと同じ論理座標を受け、親PositionとVisual Transformを逆変換します。
    // DPIはUIContextがWindow論理座標へ集約しているため、Framebuffer pixel倍率を二重適用しません。
    bool TryMapScreenPositionToTexturePixel(
        const math::Vec2& screenPosition,
        std::uint32_t& outX,
        std::uint32_t& outY) const;

    void SetTintColor(const math::Vec4& color);
    const math::Vec4& GetTintColor() const;

    void SetUV(const math::Vec2& min, const math::Vec2& max);

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    UITextureView m_TextureView;
    math::Vec4 m_TintColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec2 m_UVMin{ 0.0f, 0.0f };
    math::Vec2 m_UVMax{ 1.0f, 1.0f };
    UIImageScaleMode m_ScaleMode = UIImageScaleMode::Stretch;
};

} // namespace Raven
