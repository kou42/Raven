#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{
class UIContext;
class UIElement;

enum class UIMouseButton { None = 0, Left, Right, Middle };
enum class UIMouseEventType { Move = 0, Down, Up, Scroll, Cancel };

// PlatformのKey CodeをUI層へ直接漏らさず、UIで意味を持つKeyだけSemantic Keyへ変換します。
enum class UIKey
{
    Unknown = 0,
    Tab
};

struct UIKeyEvent
{
    UIKey Key = UIKey::Unknown;
    bool Pressed = false;
    bool Shift = false;
    bool Repeat = false;
    UIContext* Context = nullptr;
    bool Handled = false;
};

struct UIMouseEvent
{
    UIMouseEventType Type = UIMouseEventType::Move;
    UIMouseButton Button = UIMouseButton::None;
    math::Vec2 ScreenPosition{};
    math::Vec2 ScrollDelta{};
    UIContext* Context = nullptr;
    UIElement* Target = nullptr;
    UIElement* CurrentTarget = nullptr;
    UIElement* PressedTarget = nullptr;
    bool Handled = false;
};

} // namespace Raven