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

    // Rootの幅変更による再Measureで、子と親の高さ・Sibling位置が揃って更新されます。
    root.SetPreferredSize(Raven::math::Vec2(120.0f, 0.0f));
    root.BuildDrawList(drawList);
    CheckNear("wrappingPtr->GetDesiredSize().y", wrappingPtr->GetDesiredSize().y, 10.0f);
    CheckNear("containerPtr->GetDesiredSize().y", containerPtr->GetDesiredSize().y, 21.0f);
    CheckNear("root.GetDesiredSize().y", root.GetDesiredSize().y, 31.0f);
    CheckNear("siblingPtr->GetPosition().y", siblingPtr->GetPosition().y, 12.0f);
    return 0;
}
