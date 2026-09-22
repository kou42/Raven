// UIElementの幅制約付き再MeasureをGPU/Fontに依存せず検証する回帰テストです。
// 単独実行する場合はRaven UIのCore実装をリンクし、このファイルをテスト用exeの入口にしてください。
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UITextEditBuffer.h"
#include "Raven/UI/Widgets/UIInputNumber.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIComboBox.h"
#include "Raven/UI/Widgets/UITooltip.h"
#include "Raven/UI/Widgets/UITreeView.h"
#include "Raven/UI/Widgets/UITable.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <memory>
#include <string>

namespace
{
class WrappingElement final : public Raven::UIElement
{
protected:
    Raven::math::Vec2 OnMeasureContent() const override
    {
        return Raven::math::Vec2(100.0f, 10.0f);
    }

    Raven::math::Vec2 OnMeasureContentForWidth(float availableWidth) const override
    {
        // 一文字10px、10文字の仮想テキストで、Font Atlasを用意せず幅依存の高さを再現します。
        const float width = std::max(1.0f, availableWidth);
        const float lineCount = std::ceil(100.0f / width);
        return Raven::math::Vec2(std::min(100.0f, width), lineCount * 10.0f);
    }
};

bool Near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.001f;
}

void CheckNear(const char* label, float actual, float expected)
{
    // Release構成でも検証が無効化されないよう、assertではなく終了コードで失敗を通知します。
    if (Near(actual, expected) == false)
    {
        std::cerr << label << ": expected " << expected << ", actual " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void Check(bool condition, const char* label)
{
    if (condition == false)
    {
        std::cerr << label << ": failed\n";
        std::exit(EXIT_FAILURE);
    }
}

// UTF-8のCursor/SelectionとUndo/Redoを描画・GPUなしで検証します。
void TestTextEditBuffer()
{
    Raven::UITextEditBuffer buffer;
    buffer.SetText("A\xE3\x81\x82" "B");
    Check(buffer.GetLength() == 3u, "UTF-8 codepoint length");
    buffer.MoveCursor(1u);
    buffer.MoveCursor(2u, true);
    Check(buffer.GetSelectedText() == "\xE3\x81\x82", "UTF-8 selected text");
    Check(buffer.InsertText("X"), "replace selection");
    Check(buffer.GetText() == "AXB", "replace UTF-8 selection");
    Check(buffer.Undo(), "undo replace");
    Check(buffer.GetText() == "A\xE3\x81\x82" "B", "undo restores text");
    Check(buffer.GetCursor() == 2u && buffer.GetAnchor() == 1u, "undo restores selection");
    Check(buffer.Redo(), "redo replace");
    Check(buffer.GetText() == "AXB", "redo restores text");
    Check(buffer.Backspace(), "backspace");
    Check(buffer.GetText() == "AB", "backspace text");
    Check(buffer.Undo(), "undo backspace");
    Check(buffer.GetText() == "AXB", "undo backspace text");
    Check(buffer.InsertText("!"), "insert after undo");
    Check(buffer.Redo() == false, "new edit invalidates redo");
    buffer.SetText("reset");
    Check(buffer.Undo() == false, "SetText clears history");
}

// 数値確定・範囲制限・Stepと通知を、Window/Fontなしで検証します。
void TestInputNumber()
{
    Raven::UIInputNumber number;
    int notifications = 0;
    number.SetOnValueChanged([&notifications](double)
        {
            ++notifications;
        });
    number.SetRange(-10.0, 10.0);
    number.SetValue(2.0);
    number.SetStep(0.5);
    number.Increment();
    CheckNear("step up", static_cast<float>(number.GetValue()), 2.5f);
    number.Decrement();
    CheckNear("step down", static_cast<float>(number.GetValue()), 2.0f);
    Check(notifications == 2, "step change notifications");
    number.SetValue(100.0);
    CheckNear("SetValue clamp", static_cast<float>(number.GetValue()), 10.0f);
    number.Increment();
    CheckNear("step at maximum", static_cast<float>(number.GetValue()), 10.0f);
    Check(notifications == 2, "no notification at limit");
    number.GetInputText().SetText("-4.25");
    number.Commit();
    CheckNear("commit valid value", static_cast<float>(number.GetValue()), -4.25f);
    Check(number.GetEditText() == "-4.25", "commit normalizes text");
    number.GetInputText().SetText("-");
    number.Commit();
    CheckNear("incomplete input retains value", static_cast<float>(number.GetValue()), -4.25f);
    Check(number.GetEditText() == "-4.25", "incomplete input restores text");
    number.GetInputText().SetText("999");
    number.Commit();
    CheckNear("commit clamp", static_cast<float>(number.GetValue()), 10.0f);
    Check(number.GetEditText() == "10", "commit clamp normalizes text");
    number.GetInputText().SetText("1e2");
    number.Commit();
    CheckNear("exponent clamp", static_cast<float>(number.GetValue()), 10.0f);
    number.SetStep(0.0);
    CheckNear("invalid step ignored", static_cast<float>(number.GetStep()), 0.5f);
}

Raven::UIKeyEvent Press(Raven::UIKey key, bool control = false, bool shift = false)
{
    Raven::UIKeyEvent event;
    event.Key = key;
    event.Pressed = true;
    event.Control = control;
    event.Shift = shift;
    return event;
}

// UIContextを通して実際のFocus/Keyboard/Character/Clipboard配送を検証します。
void TestInputEventRouting()
{
    Raven::UIContext context;
    auto number = std::make_unique<Raven::UIInputNumber>();
    Raven::UIInputNumber* numberPtr = number.get();
    number->SetRange(-10.0, 10.0);
    number->SetValue(2.0);
    number->SetStep(0.5);
    std::string clipboard;
    number->SetClipboard([&clipboard]() { return clipboard; },
        [&clipboard](const std::string& text) { clipboard = text; });
    context.GetRootElement().AddChild(std::move(number));

    auto other = std::make_unique<Raven::UIInputText>();
    Raven::UIInputText* otherPtr = other.get();
    other->SetText("other");
    context.GetRootElement().AddChild(std::move(other));

    Raven::UIInputText* edit = &numberPtr->GetInputText();
    Check(context.SetFocus(edit), "focus numeric input");
    Check(context.GetFocusedElement() == edit, "numeric input focused");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select all");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('-')), "type minus");
    Check(numberPtr->GetEditText() == "-", "incomplete numeric prefix");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('x')), "reject invalid character");
    Check(numberPtr->GetEditText() == "-", "invalid character unchanged");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "commit invalid number");
    Check(numberPtr->GetEditText() == "2", "Enter restores committed value");

    Check(context.RouteKeyEvent(Press(Raven::UIKey::Up)), "step up routed");
    CheckNear("routed step up", static_cast<float>(numberPtr->GetValue()), 2.5f);
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "step down routed");
    CheckNear("routed step down", static_cast<float>(numberPtr->GetValue()), 2.0f);

    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select numeric value");
    clipboard = "4.5";
    Check(context.RouteKeyEvent(Press(Raven::UIKey::V, true)), "paste numeric value");
    Check(numberPtr->GetEditText() == "4.5", "numeric clipboard paste");
    clipboard = "invalid";
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select pasted number");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::V, true)), "reject invalid paste");
    Check(numberPtr->GetEditText() == "4.5", "invalid paste is atomic");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Z, true)), "undo paste");
    Check(numberPtr->GetEditText() == "2", "undo paste restores text");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Y, true)), "redo paste");
    Check(numberPtr->GetEditText() == "4.5", "redo paste restores text");

    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select for copy");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::C, true)), "copy numeric value");
    Check(clipboard == "4.5", "clipboard copy");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select for cut");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::X, true)), "cut numeric value");
    Check(numberPtr->GetEditText().empty(), "cut clears edit text");
    Check(context.SetFocus(otherPtr), "focus other widget");
    Check(numberPtr->GetEditText() == "4.5", "Focus Lost restores incomplete edit");
    Check(context.GetFocusedElement() == otherPtr, "focus transferred");
    Check(context.SetFocus(edit), "refocus numeric input");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select before focus loss");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('-')), "type invalid prefix");
    context.ClearFocus();
    Check(context.GetFocusedElement() == nullptr, "focus cleared");
    Check(numberPtr->GetEditText() == "4.5", "ClearFocus commits number");
    Check(context.SetFocus(edit), "focus before outside click");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::A, true)), "select before outside click");
    Check(context.RouteCharacterEvent(static_cast<std::uint32_t>('-')), "incomplete before outside click");
    context.RouteMouseDown(Raven::math::Vec2(-100.0f, -100.0f), Raven::UIMouseButton::Left);
    Check(context.GetFocusedElement() == nullptr, "outside click clears focus");
    Check(numberPtr->GetEditText() == "4.5", "outside click commits number");
}
// Popupの開閉・Painter順・外側入力消費・Focus/Capture破棄をGPUなしで検証します。
void TestPopupRouting()
{
    Raven::UIContext context;
    auto behind = std::make_unique<Raven::UIButton>();
    behind->SetPosition(Raven::math::Vec2(10.0f, 10.0f));
    behind->SetSize(Raven::math::Vec2(120.0f, 80.0f));
    int behindClicks = 0;
    behind->SetOnClick([&behindClicks]() { ++behindClicks; });
    Raven::UIElement* behindPtr = context.GetRootElement().AddChild(std::move(behind));

    auto popup = std::make_unique<Raven::UIElement>();
    popup->SetPosition(Raven::math::Vec2(10.0f, 10.0f));
    popup->SetSize(Raven::math::Vec2(120.0f, 80.0f));
    auto item = std::make_unique<Raven::UIButton>();
    item->SetPosition(Raven::math::Vec2(5.0f, 5.0f));
    item->SetSize(Raven::math::Vec2(60.0f, 30.0f));
    item->SetFocusable(true);
    int itemClicks = 0;
    item->SetOnClick([&itemClicks]() { ++itemClicks; });
    Raven::UIButton* itemPtr = item.get();
    popup->AddChild(std::move(item));
    Raven::UIElement* popupPtr = context.AddPopup(std::move(popup));
    Check(popupPtr != nullptr, "popup registered");
    Check(context.OpenPopup(behindPtr) == false, "reject non-popup element");
    Check(context.OpenPopup(popupPtr), "open popup");
    Check(context.GetOpenPopup() == popupPtr, "open popup identity");
    Check(context.SetFocus(itemPtr), "focus popup item");
    context.RouteMouseDown(Raven::math::Vec2(20.0f, 20.0f), Raven::UIMouseButton::Left);
    context.RouteMouseUp(Raven::math::Vec2(20.0f, 20.0f), Raven::UIMouseButton::Left);
    Check(itemClicks == 1 && behindClicks == 0, "popup is topmost");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Escape)), "Escape closes popup");
    Check(context.GetOpenPopup() == nullptr && popupPtr->IsVisible() == false, "popup hidden");
    Check(context.GetFocusedElement() == nullptr, "popup focus cleared");

    Check(context.OpenPopup(popupPtr), "reopen popup");
    Check(context.CaptureMouse(itemPtr), "capture popup item");
    Check(context.RouteMouseDown(Raven::math::Vec2(200.0f, 200.0f), Raven::UIMouseButton::Left),
        "outside Down consumed");
    Check(context.HasMouseCapture() == false, "popup capture cancelled");
    Check(context.GetOpenPopup() == nullptr, "outside Down closes popup");
    context.RouteMouseUp(Raven::math::Vec2(200.0f, 200.0f), Raven::UIMouseButton::Left);
    Check(behindClicks == 0, "outside Down not forwarded");

    // Viewport右下のAnchorでは左へClampし、下側に収まらない場合は上へ反転します。
    auto anchor = std::make_unique<Raven::UIElement>();
    anchor->SetPosition(Raven::math::Vec2(170.0f, 130.0f));
    anchor->SetSize(Raven::math::Vec2(20.0f, 20.0f));
    Raven::UIElement* anchorPtr = context.GetRootElement().AddChild(std::move(anchor));
    context.BeginFrame(Raven::math::Vec2(200.0f, 160.0f));
    Check(context.OpenPopupAt(popupPtr, anchorPtr), "anchor popup opens");
    CheckNear("anchor right clamp", popupPtr->GetPosition().x, 80.0f);
    CheckNear("anchor above flip", popupPtr->GetPosition().y, 46.0f);
    context.ClosePopup();
    Check(context.OpenPopup(popupPtr), "reopen for detach");
    Check(context.GetRootElement().GetChildren().size() >= 2u, "popup layer attached");
    // Layerの所有権をContextが保持するため、通常のRoot Childを消してもPopupは生存します。
    Check(context.GetRootElement().RemoveChild(behindPtr), "remove ordinary child");
    Check(context.GetOpenPopup() == popupPtr, "popup survives ordinary removal");
    context.ClosePopup();
    context.ClosePopup();
}
// ComboBoxの選択変更、Keyboard操作、Popup経由のMouse選択を検証します。
void TestComboBox()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto combo = std::make_unique<Raven::UIComboBox>();
    combo->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    combo->SetOptions({ "Idle", "Walk", "Run" });
    int notifications = 0;
    combo->SetOnSelectionChanged([&notifications](std::size_t, const std::string&)
        {
            ++notifications;
        });
    Raven::UIComboBox* comboPtr = combo.get();
    context.GetRootElement().AddChild(std::move(combo));
    Check(comboPtr->GetSelectedIndex() == Raven::UIComboBox::NoSelection, "combo initial selection");
    Check(comboPtr->SetSelectedIndex(1u), "combo set selection");
    Check(comboPtr->GetSelectedText() == "Walk", "combo selected text");
    Check(comboPtr->SetSelectedIndex(1u), "combo same selection");
    Check(notifications == 1, "combo unchanged selection no notification");
    Check(comboPtr->SetSelectedIndex(10u) == false, "combo invalid index");
    Check(context.SetFocus(comboPtr), "combo focus");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "combo keyboard open");
    Check(comboPtr->IsOpen(), "combo opened");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "combo next item");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Enter)), "combo keyboard select");
    Check(comboPtr->GetSelectedIndex() == 2u && comboPtr->IsOpen() == false,
        "combo keyboard selection");
    Check(notifications == 2, "combo keyboard notification");

    Check(comboPtr->Open(), "combo reopen");
    context.RouteMouseDown(Raven::math::Vec2(30.0f, 58.0f), Raven::UIMouseButton::Left);
    context.RouteMouseUp(Raven::math::Vec2(30.0f, 58.0f), Raven::UIMouseButton::Left);
    Check(comboPtr->GetSelectedIndex() == 0u && comboPtr->IsOpen() == false,
        "combo mouse selects first row");
    Check(notifications == 3, "combo mouse notification");
    Check(comboPtr->Open(), "combo open for Escape");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Escape)), "combo Escape");
    Check(comboPtr->IsOpen() == false, "combo Escape closed");
    comboPtr->SetOptions({ "Only" });
    Check(comboPtr->GetSelectedIndex() == Raven::UIComboBox::NoSelection,
        "combo options reset selection");
    Check(comboPtr->SetSelectedIndex(0u), "combo new options selection");
    Check(notifications == 4, "combo new selection notification");
    Check(context.GetRootElement().RemoveChild(comboPtr), "combo removal releases popup");
}
// TooltipのHover・入力透過・Popupとの排他・対象削除を検証します。
void TestTooltip()
{
    Raven::UIContext context;
    auto button = std::make_unique<Raven::UIButton>();
    button->SetPosition(Raven::math::Vec2(170.0f, 140.0f));
    button->SetSize(Raven::math::Vec2(30.0f, 20.0f));
    int clicks = 0;
    button->SetOnClick([&clicks]() { ++clicks; });
    Raven::UIElement* target = context.GetRootElement().AddChild(std::move(button));
    Check(context.SetTooltip(target, "Tooltip", nullptr, 0.0f), "tooltip registration");
    context.BeginFrame(Raven::math::Vec2(200.0f, 170.0f));
    context.RouteMouseMove(Raven::math::Vec2(180.0f, 150.0f));
    context.EndFrame();
    const Raven::UITooltip* tip = context.GetVisibleTooltip();
    Check(tip != nullptr && tip->GetText() == "Tooltip", "tooltip visible on hover");
    Check(tip->GetPosition().x + tip->GetSize().x <= 200.0f,
        "tooltip right viewport clamp");
    Check(tip->GetPosition().y + tip->GetSize().y <= 170.0f,
        "tooltip bottom viewport clamp");
    context.RouteMouseDown(Raven::math::Vec2(180.0f, 150.0f), Raven::UIMouseButton::Left);
    context.RouteMouseUp(Raven::math::Vec2(180.0f, 150.0f), Raven::UIMouseButton::Left);
    Check(clicks == 1, "tooltip does not block button click");
    Check(context.GetVisibleTooltip() == nullptr, "tooltip hidden on click");
    context.RouteMouseMove(Raven::math::Vec2(181.0f, 150.0f));
    Check(context.GetVisibleTooltip() != nullptr, "tooltip reappears after pointer move");
    auto popup = std::make_unique<Raven::UIElement>();
    popup->SetSize(Raven::math::Vec2(40.0f, 30.0f));
    Raven::UIElement* popupPtr = context.AddPopup(std::move(popup));
    Check(context.OpenPopup(popupPtr), "popup opens over tooltip");
    Check(context.GetVisibleTooltip() == nullptr, "popup suppresses tooltip");
    context.ClosePopup();
    context.RouteMouseMove(Raven::math::Vec2(0.0f, 0.0f));
    Check(context.GetVisibleTooltip() == nullptr, "tooltip hides on hover leave");
    Check(context.GetRootElement().RemoveChild(target), "tooltip target removed");
    Check(context.ClearTooltip(target) == false, "tooltip registration cleaned on removal");
}
// TreeViewの所有権・展開・Scroll・Keyboard/Mouse経路をFont/GPUなしで検証します。
void TestTreeView()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 48.0f));
    Raven::UITreeView* view = tree.get();
    Raven::UITreeNode* root = tree->AddRoot(1u, "Scene");
    Raven::UITreeNode* child = tree->AddNode(root, 2u, "Player");
    Raven::UITreeNode* leaf = tree->AddNode(child, 3u, "Mesh");
    Raven::UITreeNode* sibling = tree->AddRoot(4u, "Environment");
    Check(root != nullptr && child != nullptr && leaf != nullptr && sibling != nullptr, "tree nodes added");
    Check(tree->AddRoot(2u, "duplicate") == nullptr, "tree rejects duplicate id");
    Raven::UITreeNode external;
    Check(tree->AddNode(&external, 5u, "foreign") == nullptr, "tree rejects foreign parent");
    Check(tree->FindNode(3u) == leaf, "tree find nested node");
    int selections = 0;
    int expansions = 0;
    tree->SetOnSelectionChanged([&selections](std::uint64_t) { ++selections; });
    tree->SetOnExpansionChanged([&expansions](std::uint64_t, bool) { ++expansions; });
    context.GetRootElement().AddChild(std::move(tree));
    Check(view->Select(leaf), "tree select leaf");
    CheckNear("tree selected row visible", view->GetScrollOffset(), 24.0f);
    Check(view->Select(leaf), "tree select same leaf");
    Check(selections == 1, "tree unchanged selection no callback");
    Check(view->SetExpanded(root, false), "tree collapse root");
    Check(view->GetSelectedNode() == root, "tree collapse selects visible ancestor");
    CheckNear("tree collapsed range", view->GetMaxScrollOffset(), 0.0f);
    Check(expansions == 1, "tree collapse callback");
    Check(view->SetExpanded(root, true), "tree expand root");
    Check(view->SetExpanded(child, false), "tree collapse child");
    CheckNear("tree collapsed child range", view->GetMaxScrollOffset(), 24.0f);
    Check(view->SetExpanded(child, true), "tree expand child");
    CheckNear("tree expanded range", view->GetMaxScrollOffset(), 48.0f);
    view->SetScrollOffset(1000.0f);
    CheckNear("tree scroll max clamp", view->GetScrollOffset(), 48.0f);
    view->SetScrollOffset(-10.0f);
    CheckNear("tree scroll min clamp", view->GetScrollOffset(), 0.0f);
    Check(context.SetFocus(view), "tree focus");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "tree key down");
    Check(view->GetSelectedNode() == child, "tree keyboard next row");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Down)), "tree key down to leaf");
    Check(view->GetSelectedNode() == leaf, "tree keyboard leaf");
    CheckNear("tree keyboard ensure visible", view->GetScrollOffset(), 24.0f);
    Check(context.RouteMouseScroll(Raven::math::Vec2(30.0f, 30.0f),
        Raven::math::Vec2(0.0f, -1.0f)), "tree wheel scroll");
    CheckNear("tree wheel clamp", view->GetScrollOffset(), 48.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(60.0f, 32.0f),
        Raven::UIMouseButton::Left), "tree mouse selects scrolled row");
    Check(view->GetSelectedNode() == leaf, "tree scroll-aware hit test");
    context.RouteMouseUp(Raven::math::Vec2(60.0f, 32.0f), Raven::UIMouseButton::Left);
    Check(view->IsScrollBarVisible(), "tree scrollbar visible on overflow");
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 60.0f),
        Raven::UIMouseButton::Left), "tree scrollbar track click");
    CheckNear("tree track page scroll", view->GetScrollOffset(), 48.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 60.0f), Raven::UIMouseButton::Left);
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 24.0f),
        Raven::UIMouseButton::Left), "tree scrollbar thumb down");
    Check(context.HasMouseCapture(view), "tree scrollbar capture");
    context.RouteMouseMove(Raven::math::Vec2(215.0f, 60.0f));
    CheckNear("tree scrollbar drag", view->GetScrollOffset(), 48.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 60.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "tree scrollbar releases capture");
    view->SetExpanded(root, false);
    Check(view->IsScrollBarVisible() == false, "tree scrollbar hidden without overflow");
    view->Clear();
    Check(view->GetSelectedNode() == nullptr, "tree clear selection");
    Check(view->FindNode(1u) == nullptr, "tree clear nodes");
    CheckNear("tree clear scroll", view->GetScrollOffset(), 0.0f);
}
// Tableの列数検証・単一選択・Keyboard・ScrollをGPUなしで確認します。
void TestTable()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto table = std::make_unique<Raven::UITable>();
    table->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    table->SetSize(Raven::math::Vec2(200.0f, 76.0f));
    Raven::UITable* view = table.get();
    Check(table->AddColumn("Name", 100.0f), "table first column");
    Check(table->AddColumn("Type", 100.0f), "table second column");
    Check(table->AddColumn("Invalid", -1.0f) == false, "table invalid width");
    Check(table->AddRow({ "Only one" }) == false, "table rejects invalid cell count");
    Check(table->AddRow({ "A", "Mesh" }), "table first row");
    Check(table->AddRow({ "B", "Light" }), "table second row");
    Check(table->AddRow({ "C", "Camera" }), "table third row");
    Check(table->GetRows().size() == 3u, "table row count");
    int notifications = 0;
    table->SetOnSelectionChanged([&notifications](std::size_t) { ++notifications; });
    context.GetRootElement().AddChild(std::move(table));
    CheckNear("table scroll range", view->GetMaxScrollOffset(), 24.0f);
    Check(view->GetVisibleRowRange().first == 0u &&
        view->GetVisibleRowRange().second == 2u, "table initial visible row range");
    view->SetScrollOffset(24.0f);
    Check(view->GetVisibleRowRange().first == 1u &&
        view->GetVisibleRowRange().second == 3u, "table scrolled visible row range");
    view->SetScrollOffset(0.0f);
    Check(view->SelectRow(2u), "table select last");
    CheckNear("table ensure selected visible", view->GetScrollOffset(), 24.0f);
    Check(view->SelectRow(2u), "table repeat selection");
    Check(notifications == 1, "table repeat selection no callback");
    Check(view->SelectRow(3u) == false, "table invalid selection");
    Check(context.SetFocus(view), "table focus");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Home)), "table home");
    Check(view->GetSelectedIndex() == 0u, "table first selected");
    Check(context.RouteKeyEvent(Press(Raven::UIKey::End)), "table end");
    Check(view->GetSelectedIndex() == 2u, "table last selected");
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseScroll(Raven::math::Vec2(30.0f, 60.0f),
        Raven::math::Vec2(0.0f, -1.0f)), "table wheel");
    CheckNear("table wheel clamp", view->GetScrollOffset(), 24.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(30.0f, 55.0f),
        Raven::UIMouseButton::Left), "table click scrolled row");
    Check(view->GetSelectedIndex() == 1u, "table scroll-aware hit test");
    context.RouteMouseUp(Raven::math::Vec2(30.0f, 55.0f), Raven::UIMouseButton::Left);
    Check(view->SetColumnWidth(0u, 150.0f), "table set column width");
    CheckNear("table resized width", view->GetColumns()[0u].Width, 150.0f);
    Check(view->SetColumnWidth(0u, 10.0f) == false, "table reject too narrow");
    Check(view->SetColumnWidth(2u, 100.0f) == false, "table reject missing column");
    Check(context.RouteMouseDown(Raven::math::Vec2(170.0f, 30.0f),
        Raven::UIMouseButton::Left), "table resize header down");
    Check(context.HasMouseCapture(view), "table resize capture");
    context.RouteMouseMove(Raven::math::Vec2(190.0f, 30.0f));
    CheckNear("table drag resized width", view->GetColumns()[0u].Width, 170.0f);
    context.RouteMouseUp(Raven::math::Vec2(190.0f, 30.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "table resize capture released");
    Check(view->IsScrollBarVisible(), "table scrollbar overflow visible");
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 88.0f),
        Raven::UIMouseButton::Left), "table scrollbar track down");
    CheckNear("table scrollbar page", view->GetScrollOffset(), 24.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 88.0f), Raven::UIMouseButton::Left);
    view->SetScrollOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(215.0f, 52.0f),
        Raven::UIMouseButton::Left), "table scrollbar thumb down");
    Check(context.HasMouseCapture(view), "table scrollbar capture");
    context.RouteMouseMove(Raven::math::Vec2(215.0f, 80.0f));
    CheckNear("table scrollbar drag", view->GetScrollOffset(), 24.0f);
    context.RouteMouseUp(Raven::math::Vec2(215.0f, 80.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "table scrollbar release");
    view->Clear();
    Check(view->GetColumns().empty() && view->GetRows().empty(), "table clear data");
    Check(view->GetSelectedIndex() == Raven::UITable::NoSelection, "table clear selection");
    Check(view->AddColumn("Wide", 260.0f), "table horizontal wide column");
    Check(view->AddColumn("Other", 100.0f), "table horizontal second column");
    Check(view->AddRow({ "Wide cell", "Value" }), "table horizontal first row");
    Check(view->AddRow({ "Another", "Value" }), "table horizontal second row");
    Check(view->IsHorizontalScrollBarVisible(), "table horizontal scrollbar visible");
    CheckNear("table horizontal max", view->GetMaxHorizontalOffset(), 160.0f);
    view->SetHorizontalOffset(1000.0f);
    CheckNear("table horizontal clamp", view->GetHorizontalOffset(), 160.0f);
    view->SetHorizontalOffset(0.0f);
    Check(context.RouteMouseScroll(Raven::math::Vec2(30.0f, 60.0f),
        Raven::math::Vec2(-1.0f, 0.0f)), "table horizontal wheel");
    CheckNear("table horizontal wheel offset", view->GetHorizontalOffset(), 48.0f);
    view->SetHorizontalOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(210.0f, 91.0f),
        Raven::UIMouseButton::Left), "table horizontal track click");
    CheckNear("table horizontal track page", view->GetHorizontalOffset(), 160.0f);
    context.RouteMouseUp(Raven::math::Vec2(210.0f, 91.0f), Raven::UIMouseButton::Left);
    view->SetHorizontalOffset(0.0f);
    Check(context.RouteMouseDown(Raven::math::Vec2(40.0f, 91.0f),
        Raven::UIMouseButton::Left), "table horizontal thumb down");
    Check(context.HasMouseCapture(view), "table horizontal capture");
    context.RouteMouseMove(Raven::math::Vec2(130.0f, 91.0f));
    Check(view->GetHorizontalOffset() > 0.0f, "table horizontal thumb drag");
    context.RouteMouseUp(Raven::math::Vec2(130.0f, 91.0f), Raven::UIMouseButton::Left);
    Check(context.HasMouseCapture(view) == false, "table horizontal release");
    view->Clear();
    CheckNear("table horizontal clear", view->GetHorizontalOffset(), 0.0f);
    Check(view->GetVisibleRowRange().first == 0u &&
        view->GetVisibleRowRange().second == 0u, "table empty visible range");
    // 10,000行でも可視範囲はViewport内の数行だけになります。
    Check(view->AddColumn("Index", 100.0f), "table bulk column");
    for (std::size_t i = 0u; i < 10000u; ++i)
    {
        Check(view->AddRow({ std::to_string(i) }), "table bulk row");
    }
    view->SetScrollOffset(view->GetMaxScrollOffset());
    const auto visible = view->GetVisibleRowRange();
    Check(visible.second == 10000u, "table bulk last row visible");
    Check(visible.second - visible.first <= 3u, "table bulk bounded visible rows");
    // 外部モデルは全行の文字列をTableに保持せず、表示セルだけを問い合わせます。
    std::size_t externalCount = 1000000u;
    std::size_t cellQueries = 0u;
    Check(view->SetDataSource([&externalCount]() { return externalCount; },
        [&cellQueries](std::size_t row, std::size_t column)
        {
            ++cellQueries;
            return std::to_string(row) + ":" + std::to_string(column);
        }), "table set external model");
    Check(view->GetColumns().size() == 1u, "table external keeps columns");
    Check(view->GetRows().empty(), "table external does not copy rows");
    Check(view->GetRowCount() == 1000000u, "table external count");
    Check(view->AddRow({ "invalid" }) == false, "table external rejects internal rows");
    view->SetScrollOffset(view->GetMaxScrollOffset());
    const auto externalVisible = view->GetVisibleRowRange();
    Check(externalVisible.second == externalCount, "table external last visible");
    Check(externalVisible.second - externalVisible.first <= 3u, "table external bounded visible");
    Check(cellQueries == 0u, "table external no eager cell requests");
    Check(view->SelectRow(externalCount - 1u), "table external select last");
    externalCount = 2u;
    view->NotifyDataSourceChanged();
    Check(view->GetSelectedIndex() == Raven::UITable::NoSelection,
        "table external invalid selection cleared");
    CheckNear("table external scroll clamped", view->GetScrollOffset(), 0.0f);
    view->ClearDataSource();
    Check(view->HasDataSource() == false, "table external detached");
    Check(view->GetColumns().size() == 1u, "table external detach keeps columns");
    Check(view->AddRow({ "local" }), "table local rows restored");
}
} // namespace

namespace
{
// Drag & DropのCapture・閾値・Drop先・CancelをGPUなしで検証します。
class DragProbe final : public Raven::UIElement
{
public:
    int Begins = 0;
    int Drops = 0;
    int Cancels = 0;
    int Ends = 0;
    int Ups = 0;
    bool Accept = false;
    bool RemoveSourceOnDrop = false;
    bool RemoveSelfOnOver = false;
    std::string LastData;

protected:
    void OnMouseEvent(Raven::UIMouseEvent& event) override
    {
        if (event.Type == Raven::UIMouseEventType::Up)
        {
            ++Ups;
        }
    }

    bool OnDragDropEvent(Raven::UIDragDropEvent& event) override
    {
        if (event.Type == Raven::UIDragDropEventType::Over && RemoveSelfOnOver == true)
        {
            GetParent()->RemoveChild(this);
            return false;
        }
        if (event.Type == Raven::UIDragDropEventType::Begin) { ++Begins; }
        if (event.Type == Raven::UIDragDropEventType::Cancel) { ++Cancels; }
        if (event.Type == Raven::UIDragDropEventType::End) { ++Ends; }
        if (event.Type == Raven::UIDragDropEventType::Drop)
        {
            ++Drops;
            LastData = event.Payload->Data;
            if (RemoveSourceOnDrop == true && event.Source != nullptr)
            {
                event.Source->GetParent()->RemoveChild(event.Source);
            }
        }
        return Accept && event.Type == Raven::UIDragDropEventType::Over;
    }
};

void TestTreeViewDragDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    tree->SetNodeDragDropEnabled(true);
    Raven::UITreeView* view = tree.get();
    Raven::UITreeNode* root = tree->AddRoot(1u, "Root");
    Raven::UITreeNode* child = tree->AddNode(root, 2u, "Child");
    Raven::UITreeNode* destination = tree->AddRoot(3u, "Destination");
    std::uint64_t moved = 0u;
    std::uint64_t parent = 0u;
    tree->SetOnNodeDropped([&](std::uint64_t source, std::uint64_t target)
    {
        moved = source;
        parent = target;
    });
    context.GetRootElement().AddChild(std::move(tree));
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    Check(context.HasPendingDrag(), "tree node reserves drag on down");
    Check(context.GetDragPreviewText() == "Child", "tree drag preview uses node name");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 80.0f));
    Check(context.IsDragging() && context.GetDropTarget() == view, "tree accepts sibling root");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    Check(child->Parent == destination && moved == 2u && parent == 3u, "tree reparents node on drop");
    Check(context.HasMouseCapture() == false, "tree drop releases capture");
    Check(context.GetDragPreviewText().empty(), "tree drop clears preview");
    // 親を子へDropしても循環を作らないことを検証します。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 80.0f));
    Check(context.GetDropTarget() == nullptr, "tree rejects descendant target");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    Check(destination->Parent == nullptr, "tree cycle guard keeps root");

    // ChildをDestinationの前へ移すとRoot直下へ戻ります。
    Raven::UITreeView::DropPlacement placement = Raven::UITreeView::DropPlacement::Child;
    view->SetOnNodePlaced([&](std::uint64_t, std::uint64_t, Raven::UITreeView::DropPlacement value)
    {
        placement = value;
    });
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 45.0f));
    Check(context.GetDropTarget() == view, "tree accepts before insertion");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 45.0f), Raven::UIMouseButton::Left);
    Check(child->Parent == nullptr && placement == Raven::UITreeView::DropPlacement::Before,
        "tree inserts before root");
    Check(view->FindNode(2u) == child, "tree preserves moved node identity");
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 90.0f));
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 90.0f), Raven::UIMouseButton::Left);
    Check(placement == Raven::UITreeView::DropPlacement::After &&
        child->Parent == nullptr, "tree inserts after root");
}

void TestTreeViewCrossDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(600.0f, 300.0f));
    auto left = std::make_unique<Raven::UITreeView>();
    auto right = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* sourceView = left.get();
    Raven::UITreeView* targetView = right.get();
    left->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    left->SetSize(Raven::math::Vec2(200.0f, 100.0f));
    right->SetPosition(Raven::math::Vec2(260.0f, 20.0f));
    right->SetSize(Raven::math::Vec2(200.0f, 100.0f));
    left->SetNodeDragDropEnabled(true);
    right->SetNodeDragDropEnabled(true);
    Raven::UITreeNode* moved = left->AddRoot(10u, "Moved");
    Raven::UITreeNode* nested = left->AddNode(moved, 11u, "Nested");
    Raven::UITreeNode* target = right->AddRoot(20u, "Target");
    right->AddRoot(11u, "Collision");
    context.GetRootElement().AddChild(std::move(left));
    context.GetRootElement().AddChild(std::move(right));

    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 32.0f));
    Check(context.GetDropTarget() == nullptr, "external tree drop disabled by default");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 32.0f), Raven::UIMouseButton::Left);
    targetView->SetExternalNodeDropEnabled(true);
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 32.0f));
    Check(context.GetDropTarget() == nullptr, "external subtree id collision rejected");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 32.0f), Raven::UIMouseButton::Left);

    targetView->Clear();
    target = targetView->AddRoot(20u, "Target");
    Check(sourceView->Select(nested), "external source selects nested node");
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 32.0f));
    Check(context.GetDropTarget() == targetView, "external tree accepts node");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 32.0f), Raven::UIMouseButton::Left);
    Check(sourceView->FindNode(10u) == nullptr && targetView->FindNode(10u) == moved,
        "external tree transfers node ownership");
    Check(moved->Parent == target && targetView->FindNode(11u) == nested,
        "external tree preserves subtree");
    Check(sourceView->GetSelectedNode() == nullptr, "external tree clears moved selection");
}

void TestTreeViewEmptyAreaDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(600.0f, 300.0f));
    auto left = std::make_unique<Raven::UITreeView>();
    auto right = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* sourceView = left.get();
    Raven::UITreeView* targetView = right.get();
    left->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    left->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    right->SetPosition(Raven::math::Vec2(260.0f, 20.0f));
    right->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    left->SetNodeDragDropEnabled(true);
    right->SetNodeDragDropEnabled(true);
    right->SetExternalNodeDropEnabled(true);
    Raven::UITreeNode* moved = left->AddRoot(100u, "Moved");
    Raven::UITreeNode* child = left->AddNode(moved, 101u, "Child");
    std::uint64_t targetId = 999u;
    Raven::UITreeView::DropPlacement placement = Raven::UITreeView::DropPlacement::Child;
    right->SetOnNodePlaced([&](std::uint64_t, std::uint64_t target,
        Raven::UITreeView::DropPlacement value)
    {
        targetId = target;
        placement = value;
    });
    context.GetRootElement().AddChild(std::move(left));
    context.GetRootElement().AddChild(std::move(right));

    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 60.0f));
    Check(context.GetDropTarget() == targetView, "empty tree accepts external root");
    context.EndFrame();
    // 空TreeのRootEnd線はViewport上端の外へ半分消えないよう内側に描画します。
    bool hasVisibleRootEndLine = false;
    for (const auto& command : context.GetDrawList().GetCommands())
    {
        if (command.Type == Raven::UIDrawCommandType::SolidRect &&
            std::abs(command.Color.y - 0.90f) < 0.001f &&
            std::abs(command.Rect.Min.x - 260.0f) < 0.001f &&
            command.Rect.Min.y >= 20.0f && command.Rect.Max.y <= 140.0f)
        {
            hasVisibleRootEndLine = true;
        }
    }
    Check(hasVisibleRootEndLine, "empty tree root end line stays inside viewport");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 60.0f), Raven::UIMouseButton::Left);
    Check(sourceView->FindNode(100u) == nullptr && targetView->FindNode(100u) == moved,
        "empty tree receives subtree");
    Check(targetView->FindNode(101u) == child && moved->Parent == nullptr,
        "empty tree preserves descendants");
    Check(targetId == 0u && placement == Raven::UITreeView::DropPlacement::RootEnd,
        "empty tree reports root end placement");

    // 既存Rootより下の空白も、子への移動ではなくRoot末尾への移動になります。
    Raven::UITreeNode* another = sourceView->AddRoot(102u, "Another");
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(300.0f, 90.0f));
    Check(context.GetDropTarget() == targetView, "tree blank area accepts root append");
    context.RouteMouseUp(Raven::math::Vec2(300.0f, 90.0f), Raven::UIMouseButton::Left);
    Check(targetView->FindNode(102u) == another && another->Parent == nullptr,
        "blank area appends another root");
}

void TestTreeViewDragAutoScroll()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* view = tree.get();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 96.0f));
    tree->SetNodeDragDropEnabled(true);
    tree->SetDragAutoScrollStep(24.0f);
    tree->AddRoot(1u, "First");
    for (std::uint64_t id = 2u; id <= 12u; ++id)
    {
        tree->AddRoot(id, "Row");
    }
    context.GetRootElement().AddChild(std::move(tree));
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 106.0f));
    Check(view->GetScrollOffset() == 24.0f, "drag bottom edge scrolls down");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 106.0f));
    Check(view->GetScrollOffset() == 48.0f, "drag edge scrolls on subsequent move");
    context.TickDrag(0.1f);
    Check(view->GetScrollOffset() == 72.0f, "stationary drag scrolls by elapsed time");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 24.0f));
    Check(view->GetScrollOffset() == 48.0f, "drag top edge scrolls up");
    view->SetDragAutoScrollEnabled(false);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 106.0f));
    Check(view->GetScrollOffset() == 48.0f, "disabled drag auto scroll keeps offset");
    context.TickDrag(0.1f);
    Check(view->GetScrollOffset() == 48.0f, "disabled stationary drag keeps offset");
    context.CancelDrag();
}

void TestTreeViewNoOpDrop()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* view = tree.get();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 120.0f));
    tree->SetNodeDragDropEnabled(true);
    Raven::UITreeNode* first = tree->AddRoot(1u, "First");
    Raven::UITreeNode* second = tree->AddRoot(2u, "Second");
    Raven::UITreeNode* last = tree->AddRoot(3u, "Last");
    int drops = 0;
    tree->SetOnNodePlaced([&](std::uint64_t, std::uint64_t, Raven::UITreeView::DropPlacement)
    {
        ++drops;
    });
    context.GetRootElement().AddChild(std::move(tree));

    // FirstをSecondの直前へDropしても既存順序は変わりません。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 45.0f));
    Check(context.GetDropTarget() == nullptr, "before adjacent node is no-op");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 45.0f), Raven::UIMouseButton::Left);
    // SecondをFirstの直後へDropしても既存順序は変わりません。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 56.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 38.0f));
    Check(context.GetDropTarget() == nullptr, "after adjacent node is no-op");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 38.0f), Raven::UIMouseButton::Left);
    // 最後のRootを空白へDropしても無変更です。
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 80.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 110.0f));
    Check(context.GetDropTarget() == nullptr, "last root to root end is no-op");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 110.0f), Raven::UIMouseButton::Left);
    Check(drops == 0 && view->FindNode(1u) == first &&
        view->FindNode(2u) == second && view->FindNode(3u) == last,
        "no-op drops preserve identity without callbacks");
}

void TestTreeViewDragAutoExpand()
{
    Raven::UIContext context;
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    auto tree = std::make_unique<Raven::UITreeView>();
    Raven::UITreeView* view = tree.get();
    tree->SetPosition(Raven::math::Vec2(20.0f, 20.0f));
    tree->SetSize(Raven::math::Vec2(200.0f, 150.0f));
    tree->SetNodeDragDropEnabled(true);
    tree->SetDragAutoExpandDelay(0.5f);
    Raven::UITreeNode* source = tree->AddRoot(1u, "Source");
    Raven::UITreeNode* target = tree->AddRoot(2u, "Collapsed");
    Raven::UITreeNode* child = tree->AddNode(target, 3u, "Child");
    tree->SetExpanded(target, false);
    int expansions = 0;
    tree->SetOnExpansionChanged([&](std::uint64_t id, bool expanded)
    {
        if (id == 2u && expanded == true)
        {
            ++expansions;
        }
    });
    context.GetRootElement().AddChild(std::move(tree));

    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 56.0f));
    Check(context.GetDropTarget() == view && target->Expanded == false,
        "collapsed node accepts drag without immediate expansion");
    context.TickDrag(0.3f);
    Check(target->Expanded == false, "hover shorter than delay keeps node collapsed");
    context.TickDrag(0.2f);
    Check(target->Expanded == true && expansions == 1 && view->FindNode(3u) == child,
        "stationary drag expands collapsed node once");
    context.TickDrag(0.5f);
    Check(expansions == 1, "expanded node does not repeat expansion callback");
    context.CancelDrag();

    view->SetExpanded(target, false);
    view->SetDragAutoExpandEnabled(false);
    context.RouteMouseDown(Raven::math::Vec2(70.0f, 32.0f), Raven::UIMouseButton::Left);
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 56.0f));
    context.TickDrag(1.0f);
    Check(target->Expanded == false, "disabled drag auto expand keeps node collapsed");
    context.CancelDrag();
    Check(view->FindNode(1u) == source, "cancelled drag retains source node");
}

void TestDragDropRouting()
{
    Raven::UIContext context;
    auto source = std::make_unique<DragProbe>();
    auto target = std::make_unique<DragProbe>();
    DragProbe* sourcePtr = source.get();
    DragProbe* targetPtr = target.get();
    source->SetPosition(Raven::math::Vec2(0.0f, 0.0f));
    source->SetSize(Raven::math::Vec2(40.0f, 40.0f));
    target->SetPosition(Raven::math::Vec2(60.0f, 0.0f));
    target->SetSize(Raven::math::Vec2(40.0f, 40.0f));
    target->Accept = true;
    context.GetRootElement().AddChild(std::move(source));
    context.GetRootElement().AddChild(std::move(target));
    context.RouteMouseDown(Raven::math::Vec2(10.0f, 10.0f), Raven::UIMouseButton::Left);
    Check(context.BeginDrag(sourcePtr, {"test/item", "payload"}, Raven::math::Vec2(10.0f, 10.0f)), "drag begins pending");
    context.RouteMouseMove(Raven::math::Vec2(12.0f, 10.0f));
    Check(sourcePtr->Begins == 0 && context.IsDragging() == false, "drag threshold");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 10.0f));
    Check(sourcePtr->Begins == 1 && context.GetDropTarget() == targetPtr, "captured drag finds target");
    context.BeginFrame(Raven::math::Vec2(400.0f, 300.0f));
    context.EndFrame();
    const auto& previewCommands = context.GetDrawList().GetCommands();
    Check(previewCommands.size() >= 2u, "drag preview adds overlay commands");
    Check(previewCommands.back().Type == Raven::UIDrawCommandType::SolidRect,
        "drag preview accent is a rectangle");
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 10.0f), Raven::UIMouseButton::Left);
    Check(targetPtr->Drops == 1 && targetPtr->LastData == "payload", "payload drop");
    Check(sourcePtr->Ends == 1 && sourcePtr->Ups == 0, "drop suppresses click");
    Check(context.HasMouseCapture() == false && context.HasPendingDrag() == false, "drop clears capture");

    Check(context.BeginDrag(sourcePtr, {"test/item", "cancel"}, Raven::math::Vec2(10.0f, 10.0f)), "second drag");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 10.0f));
    Check(context.RouteKeyEvent(Press(Raven::UIKey::Escape)), "escape consumes drag");
    Check(sourcePtr->Cancels == 1 && context.HasPendingDrag() == false, "escape cancels");
    Check(context.HasMouseCapture() == false, "escape releases capture");
    Check(context.GetDragPreviewText().empty(), "cancel clears preview");
    // Drop callbackがSourceを削除してもEndで解放済みPointerへアクセスしません。
    targetPtr->RemoveSourceOnDrop = true;
    Check(context.BeginDrag(sourcePtr, {"test/item", "remove"}, Raven::math::Vec2(10.0f, 10.0f)), "remove-source drag");
    context.RouteMouseMove(Raven::math::Vec2(70.0f, 10.0f));
    context.RouteMouseUp(Raven::math::Vec2(70.0f, 10.0f), Raven::UIMouseButton::Left);
    Check(targetPtr->Drops == 2 && context.HasPendingDrag() == false, "drop removes source safely");

    // Over callbackが候補自身を削除しても、削除済み候補のParentを辿りません。
    auto disposable = std::make_unique<DragProbe>();
    DragProbe* disposablePtr = disposable.get();
    disposable->SetPosition(Raven::math::Vec2(0.0f, 50.0f));
    disposable->SetSize(Raven::math::Vec2(40.0f, 40.0f));
    disposable->RemoveSelfOnOver = true;
    context.GetRootElement().AddChild(std::move(disposable));
    Check(context.BeginDrag(targetPtr, {"test/item", "remove-target"}, Raven::math::Vec2(70.0f, 10.0f)), "remove-target drag");
    context.RouteMouseMove(Raven::math::Vec2(10.0f, 60.0f));
    Check(context.GetDropTarget() != disposablePtr, "removed candidate is not drop target");
    context.CancelDrag();
    Check(context.HasMouseCapture() == false, "remove-target cancel releases capture");
}

} // namespace

int main()
{
    TestDragDropRouting();
    TestTreeViewDragDrop();
    TestTreeViewCrossDrop();
    TestTreeViewEmptyAreaDrop();
    TestTreeViewDragAutoScroll();
    TestTreeViewNoOpDrop();
    TestTreeViewDragAutoExpand();
    TestTextEditBuffer();
    TestInputNumber();
    TestInputEventRouting();
    TestPopupRouting();
    TestComboBox();
    TestTooltip();
    TestTreeView();
    TestTable();
    // 共通Scrollbar幾何: HeaderなしTreeとHeaderありTableでTrack原点だけが異なります。
    Raven::UIScrollBarMetrics metrics{ 48.0f, 72.0f, 0.0f };
    Check(metrics.IsVisible(), "scrollbar metrics visible");
    CheckNear("scrollbar metrics max", metrics.MaxOffset(), 24.0f);
    CheckNear("scrollbar metrics thumb", metrics.ThumbLength(), 32.0f);
    CheckNear("scrollbar metrics start", metrics.ThumbStart(), 0.0f);
    CheckNear("scrollbar metrics drag end", metrics.OffsetFromThumbStart(16.0f), 24.0f);
    metrics.Offset = 12.0f;
    CheckNear("scrollbar metrics middle", metrics.ThumbStart(), 8.0f);
    metrics.Content = 24.0f;
    Check(metrics.IsVisible() == false, "scrollbar metrics hidden");
    CheckNear("scrollbar metrics no scroll", metrics.OffsetFromThumbStart(16.0f), 0.0f);

    // Widget個別ClipはWorld Transform後に親Clipと交差することを検証します。
    Raven::UIDrawList clipDrawList;
    clipDrawList.AddRect(Raven::math::Vec2(0.0f, 0.0f),
        Raven::math::Vec2(20.0f, 20.0f),
        Raven::math::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    Raven::UIRect localClip;
    localClip.Min = Raven::math::Vec2(2.0f, 3.0f);
    localClip.Max = Raven::math::Vec2(12.0f, 13.0f);
    clipDrawList.ApplyClip(0u, Raven::UIClipRect::FromRect(localClip));
    Raven::UITransform2D clipTransform = Raven::UITransform2D::Identity();
    clipTransform.Translation = Raven::math::Vec2(10.0f, 20.0f);
    clipDrawList.ApplyTransform(0u, clipTransform);
    Raven::UIRect ancestorClip;
    ancestorClip.Min = Raven::math::Vec2(15.0f, 20.0f);
    ancestorClip.Max = Raven::math::Vec2(40.0f, 40.0f);
    clipDrawList.ApplyClip(0u, Raven::UIClipRect::FromRect(ancestorClip));
    CheckNear("transformed cell clip min x", clipDrawList.GetCommands()[0u].Clip.Rect.Min.x, 15.0f);
    CheckNear("transformed cell clip min y", clipDrawList.GetCommands()[0u].Clip.Rect.Min.y, 23.0f);
    CheckNear("transformed cell clip max x", clipDrawList.GetCommands()[0u].Clip.Rect.Max.x, 22.0f);
    CheckNear("transformed cell clip max y", clipDrawList.GetCommands()[0u].Clip.Rect.Max.y, 33.0f);

    Raven::UIDrawList drawList;
    Raven::UIElement root;
    root.SetLayoutMode(Raven::UILayoutMode::Vertical);
    root.SetPreferredSize(Raven::math::Vec2(60.0f, 0.0f));
    root.SetPadding(5.0f);
    root.SetSpacing(3.0f);

    auto container = std::make_unique<Raven::UIElement>();
    container->SetLayoutMode(Raven::UILayoutMode::Vertical);
    container->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    container->SetPadding(2.0f);

    auto wrapping = std::make_unique<WrappingElement>();
    wrapping->SetHorizontalAlignment(Raven::UIAlignment::Stretch);
    WrappingElement* wrappingPtr = wrapping.get();
    container->AddChild(std::move(wrapping));

    auto sibling = std::make_unique<Raven::UIElement>();
    sibling->SetPreferredSize(Raven::math::Vec2(10.0f, 7.0f));
    Raven::UIElement* siblingPtr = sibling.get();
    container->AddChild(std::move(sibling));

    Raven::UIElement* containerPtr = root.AddChild(std::move(container));
    root.BuildDrawList(drawList);

    // Root content幅50、Container content幅46なので仮想テキストは3行になります。
    CheckNear("wrappingPtr->GetDesiredSize().y", wrappingPtr->GetDesiredSize().y, 30.0f);
    CheckNear("containerPtr->GetDesiredSize().y", containerPtr->GetDesiredSize().y, 41.0f);
    CheckNear("root.GetDesiredSize().y", root.GetDesiredSize().y, 51.0f);
    CheckNear("siblingPtr->GetPosition().y", siblingPtr->GetPosition().y, 32.0f);

    // さらに幅を縮めた場合も、古い高さを流用せず5行へ増加することを確認します。
    root.SetPreferredSize(Raven::math::Vec2(35.0f, 0.0f));
    root.BuildDrawList(drawList);
    CheckNear("narrow wrapping height", wrappingPtr->GetDesiredSize().y, 50.0f);
    CheckNear("narrow container height", containerPtr->GetDesiredSize().y, 61.0f);
    CheckNear("narrow root height", root.GetDesiredSize().y, 71.0f);
    CheckNear("narrow sibling position", siblingPtr->GetPosition().y, 52.0f);

    // Rootの幅変更による再Measureで、子と親の高さ・Sibling位置が揃って更新されます。
    root.SetPreferredSize(Raven::math::Vec2(120.0f, 0.0f));
    root.BuildDrawList(drawList);
    CheckNear("wrappingPtr->GetDesiredSize().y", wrappingPtr->GetDesiredSize().y, 10.0f);
    CheckNear("containerPtr->GetDesiredSize().y", containerPtr->GetDesiredSize().y, 21.0f);
    CheckNear("root.GetDesiredSize().y", root.GetDesiredSize().y, 31.0f);
    CheckNear("siblingPtr->GetPosition().y", siblingPtr->GetPosition().y, 12.0f);

    // Measure対象外の子もArrangeには参加します。親の高さだけが減ることを検証します。
    siblingPtr->SetAffectsParentMeasure(false);
    root.BuildDrawList(drawList);
    CheckNear("excluded container height", containerPtr->GetDesiredSize().y, 14.0f);
    CheckNear("excluded root height", root.GetDesiredSize().y, 24.0f);
    CheckNear("excluded sibling position", siblingPtr->GetPosition().y, 12.0f);

    siblingPtr->SetAffectsParentMeasure(true);
    root.BuildDrawList(drawList);
    CheckNear("restored container height", containerPtr->GetDesiredSize().y, 21.0f);
    CheckNear("restored root height", root.GetDesiredSize().y, 31.0f);

    // Marginは外側の必要高さにだけ加算し、折り返し自体の行数には影響しません。
    wrappingPtr->SetMargin(Raven::UIThickness(1.0f, 2.0f));
    root.BuildDrawList(drawList);
    CheckNear("margin wrapping height", wrappingPtr->GetDesiredSize().y, 10.0f);
    CheckNear("margin container height", containerPtr->GetDesiredSize().y, 25.0f);
    CheckNear("margin root height", root.GetDesiredSize().y, 35.0f);
    CheckNear("margin sibling position", siblingPtr->GetPosition().y, 16.0f);

    // 非表示の子はMeasure集約とArrangeの双方から除外されます。
    siblingPtr->SetVisible(false);
    root.BuildDrawList(drawList);
    CheckNear("hidden container height", containerPtr->GetDesiredSize().y, 18.0f);
    CheckNear("hidden root height", root.GetDesiredSize().y, 28.0f);
    return 0;
}
