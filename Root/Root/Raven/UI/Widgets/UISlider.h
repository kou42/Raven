#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

#include <functional>

namespace Raven
{

// ============================================================================
// UISlider
// ============================================================================
// Mouse Captureを利用する最小ドラッグWidgetです。
// 左Button押下時に自分自身をCaptureし、PointerがElement外へ出てもMouse Moveを受け続けます。
// Mouse UpでCaptureを解放するため、SliderのDrag所有権が途中で別Widgetへ移ることはありません。
class UISlider final : public UIElement
{
public:
    using ValueChangedHandler = std::function<void(float)>;

    void SetRange(float minimum, float maximum);
    void SetValue(float value);
    void SetOnValueChanged(ValueChangedHandler handler);

    void SetTrackColor(const math::Vec4& color);
    void SetFillColor(const math::Vec4& color);
    void SetThumbColor(const math::Vec4& color);
    void SetHoveredThumbColor(const math::Vec4& color);
    void SetActiveThumbColor(const math::Vec4& color);

    float GetMinimum() const;
    float GetMaximum() const;
    float GetValue() const;
    bool IsDragging() const;

protected:
    void OnMouseEvent(UIMouseEvent& event) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    void UpdateValueFromScreenPosition(const math::Vec2& screenPosition);
    math::Vec2 GetAbsolutePosition() const;
    float GetNormalizedValue() const;

private:
    float m_Minimum = 0.0f;
    float m_Maximum = 1.0f;
    float m_Value = 0.0f;
    float m_TrackHeight = 6.0f;
    float m_ThumbWidth = 14.0f;
    math::Vec4 m_TrackColor{ 0.16f, 0.18f, 0.24f, 1.0f };
    math::Vec4 m_FillColor{ 0.14f, 0.46f, 0.82f, 1.0f };
    math::Vec4 m_ThumbColor{ 0.72f, 0.76f, 0.84f, 1.0f };
    math::Vec4 m_HoveredThumbColor{ 0.86f, 0.89f, 0.95f, 1.0f };
    math::Vec4 m_ActiveThumbColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    ValueChangedHandler m_OnValueChanged;
    bool m_Dragging = false;
};

} // namespace Raven
