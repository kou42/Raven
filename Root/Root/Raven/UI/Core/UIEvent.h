#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{

class UIContext;
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
    Up,
    Scroll,
    Cancel
};

// ============================================================================
// UIMouseEvent
// ============================================================================
// UIContextからUIElement TreeへRoutingする最小マウスイベントです。
// Targetは最初にHitしたElement、またはCapture中のElementを保持し、CurrentTargetはBubble中に現在処理している
// Elementへ更新します。Handledをtrueにすると、それより上のParentへの伝播を停止できます。
//
// ContextはこのEventを発行したUIContextです。WidgetはCaptureの開始/解放など、
// 入力Routingの所有権操作が必要な場合だけContextへアクセスします。
// Hover / Pressed / Click / Scrollはこの生入力イベントの上に構築し、Hit TestやRouting自体には
// Widget固有の状態を持たせない設計とします。
// PressedTargetは左Mouse Downを開始したElementをMouse Upまで保持し、Click成立判定に利用します。
// ScrollDeltaはPlatformから届いたwheel/trackpadの論理Offsetで、Pixel量への変換はWidget側の責務です。
// Cancelは通常のMouse入力ではなく、Context側からCaptureを強制終了するときにCapture所有者へ通知します。
// Window Focus LostやWidget Tree再構築など、Mouse Upが届かない経路でもDrag状態を確実に掃除するためのEventです。
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
