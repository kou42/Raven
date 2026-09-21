// UIElementの幅制約付き再MeasureをGPU/Fontに依存せず検証する回帰テストです。
// 単独実行する場合はRaven UIのCore実装をリンクし、このファイルをテスト用exeの入口にしてください。
#include "Raven/UI/Core/UIElement.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <memory>

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
} // namespace

int main()
{
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
