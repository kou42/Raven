#include "Raven/Editor/EditorShortcutRouter.h"
#include "Raven/Editor/EditorCommandHistory.h"

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

bool TryHandleEditorHistoryShortcut(const UIContext& context,
    const EditorShortcutRoutingContext& routingContext, const UIKeyEvent& event)
{
    if (event.Pressed == false || event.Repeat == true ||
        (event.Control == false && event.Super == false) ||
        context.HasMouseCapture() == true || context.HasPendingDrag() == true)
    {
        return false;
    }
    const EditorShortcutTarget owner = ResolveEditorShortcutTarget(context, routingContext);
    if (owner == EditorShortcutTarget::TextInput || owner == EditorShortcutTarget::Popup ||
        owner == EditorShortcutTarget::Gizmo)
    {
        return false;
    }
    if (event.Key == UIKey::Z)
    {
        if (event.Shift == true)
        {
            RedoEditorCommand();
        }
        else
        {
            UndoEditorCommand();
        }
        return true;
    }
    if (event.Key == UIKey::Y)
    {
        RedoEditorCommand();
        return true;
    }
    return false;
}

} // namespace Raven
