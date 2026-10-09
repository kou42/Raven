#include "Raven/Editor/EditorShortcutRouter.h"

#include "Raven/UI/Core/UIContext.h"

namespace Raven
{

namespace
{

bool OwnsShortcutInput(const UIInteractionState& state)
{
    return state.Focused == true || state.Active == true || state.Captured == true;
}

} // namespace

EditorShortcutTarget ResolveEditorShortcutTarget(
    const UIContext& context,
    const EditorShortcutRoutingContext& routingContext)
{
    // Popup表示中は、FocusがPopup外に残っていてもEscapeやNavigationをPopupへ限定します。
    if (context.GetOpenPopup() != nullptr)
    {
        return EditorShortcutTarget::Popup;
    }

    // Text編集のCopy/Paste/Undo等をEditor全体のShortcutより先に処理します。
    if (context.IsTextInputFocused() == true)
    {
        return EditorShortcutTarget::TextInput;
    }

    // Gizmo Drag中はViewportやGlobalの操作へ同じKeyを重複配送しません。
    if (routingContext.GizmoActive == true)
    {
        return EditorShortcutTarget::Gizmo;
    }

    const UIInteractionState viewportState =
        context.GetInteractionState(routingContext.Viewport);
    if (OwnsShortcutInput(viewportState) == true)
    {
        return EditorShortcutTarget::Viewport;
    }

    const UIInteractionState panelState =
        context.GetInteractionState(routingContext.Panel);
    if (OwnsShortcutInput(panelState) == true)
    {
        return EditorShortcutTarget::Panel;
    }

    return EditorShortcutTarget::Global;
}

} // namespace Raven
