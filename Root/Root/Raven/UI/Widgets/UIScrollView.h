#pragma once

#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

// ============================================================================
// UIScrollView
// ============================================================================
// 1つのContent ElementをViewport内へClipし、Scroll Offsetで移動させるContainerです。
//
// Scroll自体はLayout Treeを作り直さず、ContentのAbsolute Positionだけを更新します。
// Viewport SizeはUIScrollView自身のSize、Content SizeはContentのLayout結果から求めます。
// Scrollbar描画は後続段階へ分離し、まずはWheel / Trackpad入力とClipの正しい組み合わせを成立させます。
class UIScrollView final : public UIElement
{
public:
    UIScrollView();

    // ScrollViewは単一Contentを所有します。
    // 既存Contentがある場合はClearChildren()してから置き換えます。
    UIElement* SetContent(Scope<UIElement> content);
    UIElement* GetContent();
    const UIElement* GetContent() const;

    void SetScrollOffset(const math::Vec2& value);
    const math::Vec2& GetScrollOffset() const;
    math::Vec2 GetMaxScrollOffset() const;

    // Content / Viewport Size変更後に現在Offsetを新しい範囲へClampします。
    void RefreshScrollRange();

    // GLFW Scroll Offset 1.0あたりに移動するPixel量です。
    void SetWheelScrollStep(float value);
    float GetWheelScrollStep() const;

protected:
    void OnMouseEvent(UIMouseEvent& event) override;

private:
    math::Vec2 ResolveViewportSize() const;
    math::Vec2 ResolveContentSize() const;
    void ApplyContentPosition();

private:
    math::Vec2 m_ScrollOffset{};
    float m_WheelScrollStep = 40.0f;
};

} // namespace Raven
