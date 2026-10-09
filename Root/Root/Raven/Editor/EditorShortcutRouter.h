#pragma once

namespace Raven
{

class UIContext;
class UIElement;

// Editor Shortcutを処理する所有者です。
// 値の並びではなくResolveEditorShortcutTarget()の明示的な判定順を優先順位とします。
enum class EditorShortcutTarget
{
    Global = 0,
    Panel,
    Viewport,
    Gizmo,
    TextInput,
    Popup
};

struct EditorShortcutRoutingContext
{
    // 現在Shortcutを受け取ろうとしているPanelと、その中のScene/Game Viewです。
    // 非所有参照であり、UIContextのTreeへ接続されている間だけ渡してください。
    const UIElement* Panel = nullptr;
    const UIElement* Viewport = nullptr;
    bool GizmoActive = false;
};

// Popup > TextInput > Gizmo > Viewport > Panel > Globalの順で現在の入力所有者を返します。
// 呼び出し側は自分のTargetと一致したShortcutだけを処理し、他の所有者へ入力を漏らしません。
EditorShortcutTarget ResolveEditorShortcutTarget(
    const UIContext& context,
    const EditorShortcutRoutingContext& routingContext);

} // namespace Raven
