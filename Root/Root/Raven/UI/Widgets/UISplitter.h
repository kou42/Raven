#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

#include <functional>

namespace Raven
{

// Splitterの見た目方向です。
// Verticalは縦長Handleを左右へDragし、Horizontalは横長Handleを上下へDragします。
enum class UISplitterOrientation
{
    Vertical = 0,
    Horizontal
};

// ============================================================================
// UISplitter
// ============================================================================
// Mouse Captureを利用する汎用的な分割Handleです。
// Splitter自身は隣接Panelを直接所有せず、Pointer移動量だけをCallbackへ通知します。
// これによりEditor Panel、Property列幅、Game View分割など、対象のLifetimeやLayout規則を
// Widget側へ持ち込まずに同じDrag基盤を再利用できます。
class UISplitter final : public UIElement
{
public:
    using DragDeltaHandler = std::function<void(float)>;

    void SetOrientation(UISplitterOrientation orientation);
    void SetOnDragDelta(DragDeltaHandler handler);

    void SetNormalColor(const math::Vec4& color);
    void SetHoveredColor(const math::Vec4& color);
    void SetActiveColor(const math::Vec4& color);

    UISplitterOrientation GetOrientation() const;
    bool IsDragging() const;

protected:
    void OnMouseEvent(UIMouseEvent& event) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    float GetAxisPosition(const math::Vec2& screenPosition) const;
    void UpdateDrag(const math::Vec2& screenPosition);

private:
    UISplitterOrientation m_Orientation = UISplitterOrientation::Vertical;
    math::Vec4 m_NormalColor{ 0.18f, 0.20f, 0.26f, 1.0f };
    math::Vec4 m_HoveredColor{ 0.28f, 0.32f, 0.42f, 1.0f };
    math::Vec4 m_ActiveColor{ 0.18f, 0.48f, 0.82f, 1.0f };
    DragDeltaHandler m_OnDragDelta;
    float m_LastAxisPosition = 0.0f;
    bool m_Dragging = false;
};

} // namespace Raven
