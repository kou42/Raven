#pragma once

#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

class UIScrollBarVisual;

// ============================================================================
// UIScrollView
// ============================================================================
// 1つのContent ElementをViewport内へClipし、Scroll Offsetで移動させるContainerです。
//
// ScrollbarはScrollView内部のUIElementとしてContentより後に配置します。
// そのためPainter's OrderだけでなくHit TestでもScrollbarがContentより手前になり、
// Thumb Drag / Track Clickが背後のButton等へ奪われない構造になります。
class UIScrollView final : public UIElement
{
public:
    UIScrollView();

    // ScrollViewは単一Contentを所有します。
    // 既存Contentがある場合は内部Scrollbarも含めてTreeを安全に再構築します。
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

    // Scrollbarは既定でAuto表示です。Axisを無効化するとOverflowしていても表示・操作しません。
    void SetVerticalScrollBarEnabled(bool value);
    bool IsVerticalScrollBarEnabled() const;
    bool IsVerticalScrollBarVisible() const;
    void SetHorizontalScrollBarEnabled(bool value);
    bool IsHorizontalScrollBarEnabled() const;
    bool IsHorizontalScrollBarVisible() const;

    void SetScrollBarThickness(float value);
    float GetScrollBarThickness() const;
    void SetMinimumThumbLength(float value);
    float GetMinimumThumbLength() const;
    void SetPageScrollFactor(float value);
    float GetPageScrollFactor() const;

    void SetScrollBarTrackColor(const math::Vec4& value);
    const math::Vec4& GetScrollBarTrackColor() const;
    void SetScrollBarThumbColor(const math::Vec4& value);
    const math::Vec4& GetScrollBarThumbColor() const;

protected:
    void OnMouseEvent(UIMouseEvent& event) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    enum class ScrollBarDragAxis
    {
        None = 0,
        Horizontal,
        Vertical
    };

    void CreateScrollBarElements();
    void SyncScrollBars();
    void ApplyContentPosition();
    void EndThumbDrag();
    bool HandleScrollBarMouseDown(UIMouseEvent& event);
    bool HandleThumbDrag(UIMouseEvent& event);
    void PageScroll(bool vertical, bool forward);

    math::Vec2 ResolveViewportSize() const;
    math::Vec2 ResolveContentSize() const;

private:
    UIElement* m_Content = nullptr;
    UIScrollBarVisual* m_VerticalScrollBar = nullptr;
    UIScrollBarVisual* m_HorizontalScrollBar = nullptr;

    math::Vec2 m_ScrollOffset{};
    float m_WheelScrollStep = 40.0f;
    float m_ScrollBarThickness = 10.0f;
    float m_MinimumThumbLength = 20.0f;
    float m_PageScrollFactor = 0.9f;
    math::Vec4 m_ScrollBarTrackColor{ 0.06f, 0.07f, 0.09f, 0.75f };
    math::Vec4 m_ScrollBarThumbColor{ 0.42f, 0.45f, 0.52f, 0.95f };

    ScrollBarDragAxis m_DragAxis = ScrollBarDragAxis::None;
    float m_DragGrabOffset = 0.0f;
    bool m_VerticalScrollBarEnabled = true;
    bool m_HorizontalScrollBarEnabled = true;
    bool m_VerticalScrollBarVisible = false;
    bool m_HorizontalScrollBarVisible = false;
};

} // namespace Raven
