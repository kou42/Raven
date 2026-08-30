#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{

class UIElement;

enum class UIMouseButton
{
    None = 0,
    Left,
    Right,
    Middle
};

enum class UIMouseEventType
{
    Move = 0,
    Down,
    Up
};

// ============================================================================
// UIMouseEvent
// ============================================================================
// UIContextからUIElement TreeへRoutingする最小マウスイベントです。
// Targetは最初にHitしたElementを保持し、CurrentTargetはBubble中に現在処理している
// Elementへ更新します。Handledをtrueにすると、それより上のParentへの伝播を停止できます。
//
// Hover / Pressed / Clickはこの生入力イベントの上に構築し、Hit TestやRouting自体には
// Widget固有の状態を持たせない設計とします。
struct UIMouseEvent
{
    UIMouseEventType Type = UIMouseEventType::Move;
    UIMouseButton Button = UIMouseButton::None;
    math::Vec2 ScreenPosition{};
    UIElement* Target = nullptr;
    UIElement* CurrentTarget = nullptr;
    bool Handled = false;
};

} // namespace Raven
