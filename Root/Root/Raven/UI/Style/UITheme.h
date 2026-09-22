#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{

// Widgetの状態別外観です。描画時にUIContextから参照し、Theme切替を即座に反映します。
struct UIButtonStyle
{
    math::Vec4 NormalColor{ 0.24f, 0.24f, 0.28f, 1.0f };
    math::Vec4 HoveredColor{ 0.32f, 0.32f, 0.38f, 1.0f };
    math::Vec4 PressedColor{ 0.16f, 0.16f, 0.20f, 1.0f };
    math::Vec4 FocusedColor{ 0.38f, 0.46f, 0.68f, 1.0f };
};

struct UISliderStyle
{
    math::Vec4 TrackColor{ 0.16f, 0.18f, 0.24f, 1.0f };
    math::Vec4 FillColor{ 0.14f, 0.46f, 0.82f, 1.0f };
    math::Vec4 ThumbColor{ 0.72f, 0.76f, 0.84f, 1.0f };
    math::Vec4 HoveredThumbColor{ 0.86f, 0.89f, 0.95f, 1.0f };
    math::Vec4 ActiveThumbColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec4 FocusedThumbColor{ 0.48f, 0.72f, 1.0f, 1.0f };
};

struct UIPanelStyle
{
    math::Vec4 BackgroundColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// UIContextごとに値として保持します。異なるWindow/描画Target間でTheme変更が漏れません。
// 初期値は既存Widgetの外観と同じにして、未設定のUIの描画を変更しません。
struct UITheme
{
    UIButtonStyle Button;
    UIPanelStyle Panel;
    UISliderStyle Slider;

    static UITheme CreateDefaultDark()
    {
        return UITheme{};
    }

    static UITheme CreateDefaultLight()
    {
        UITheme theme;
        theme.Button.NormalColor = math::Vec4(0.86f, 0.87f, 0.90f, 1.0f);
        theme.Button.HoveredColor = math::Vec4(0.77f, 0.81f, 0.88f, 1.0f);
        theme.Button.PressedColor = math::Vec4(0.66f, 0.72f, 0.83f, 1.0f);
        theme.Button.FocusedColor = math::Vec4(0.57f, 0.70f, 0.94f, 1.0f);
        theme.Slider.TrackColor = math::Vec4(0.77f, 0.79f, 0.83f, 1.0f);
        theme.Slider.FillColor = math::Vec4(0.17f, 0.43f, 0.76f, 1.0f);
        theme.Slider.ThumbColor = math::Vec4(0.34f, 0.39f, 0.48f, 1.0f);
        theme.Slider.HoveredThumbColor = math::Vec4(0.23f, 0.32f, 0.46f, 1.0f);
        theme.Slider.ActiveThumbColor = math::Vec4(0.14f, 0.26f, 0.45f, 1.0f);
        theme.Slider.FocusedThumbColor = math::Vec4(0.15f, 0.42f, 0.77f, 1.0f);
        theme.Panel.BackgroundColor = math::Vec4(0.96f, 0.96f, 0.97f, 1.0f);
        return theme;
    }
};

} // namespace Raven
