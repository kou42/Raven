// UIElementの幅制約付き再MeasureをGPU/Fontに依存せず検証する回帰テストです。
// 単独実行する場合はRaven UIのCore実装をリンクし、このファイルをテスト用exeの入口にしてください。
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UITextEditBuffer.h"
#include "Raven/UI/Widgets/UIInputNumber.h"

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
} // namespace

int main()
{
    TestTextEditBuffer();
    TestInputNumber();
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
