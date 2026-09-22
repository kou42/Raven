#pragma once

#include "Raven/UI/Immediate/UIImmediateContext.h"
#include "Raven/UI/Widgets/UIButton.h"

#include <cstdlib>
#include <iostream>

namespace
{

void CheckImmediate(bool condition, const char* message)
{
    if (condition == false)
    {
        std::cerr << "Immediate UI: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void TestUIImmediateContext()
{
    Raven::UIContext context;
    Raven::UIImmediateContext immediate(context);
    CheckImmediate(immediate.EndFrame() == false, "EndFrame without BeginFrame");
    CheckImmediate(immediate.BeginFrame() == true, "first BeginFrame");
    CheckImmediate(immediate.BeginFrame() == false, "nested BeginFrame");

    auto* first = immediate.GetOrCreate<Raven::UIButton>("action");
    CheckImmediate(first != nullptr, "create button");
    CheckImmediate(immediate.GetOrCreate<Raven::UIButton>("action") == nullptr,
        "duplicate ID in frame");
    CheckImmediate(immediate.PushID("left") == true, "push left");
    auto* left = immediate.GetOrCreate<Raven::UIButton>("action");
    CheckImmediate(left != nullptr && left != first, "scoped ID");
    CheckImmediate(immediate.PopID() == true, "pop left");
    CheckImmediate(immediate.PopID() == false, "unbalanced pop");
    CheckImmediate(immediate.EndFrame() == true, "first EndFrame");
    CheckImmediate(immediate.GetCachedWidgetCount() == 2u, "cached widgets");

    CheckImmediate(immediate.BeginFrame() == true, "second BeginFrame");
    CheckImmediate(immediate.GetOrCreate<Raven::UIButton>("action") == first,
        "stable widget identity");
    CheckImmediate(immediate.EndFrame() == true, "second EndFrame");
    CheckImmediate(immediate.GetCachedWidgetCount() == 1u, "unused widget removed");

    CheckImmediate(immediate.BeginFrame() == true, "third BeginFrame");
    auto* container = immediate.BeginContainer<Raven::UIElement>("group");
    CheckImmediate(container != nullptr, "container created");
    auto* child = immediate.GetOrCreate<Raven::UIButton>("child");
    CheckImmediate(child != nullptr && child->GetParent() == container,
        "child belongs to container");
    CheckImmediate(immediate.PopID() == false, "container cannot be popped as ID");
    CheckImmediate(immediate.EndFrame() == false, "unclosed container");
    CheckImmediate(immediate.EndContainer() == true, "close container");
    CheckImmediate(immediate.EndFrame() == true, "third EndFrame");

    CheckImmediate(immediate.BeginFrame() == true, "fourth BeginFrame");
    CheckImmediate(immediate.EndFrame() == true, "fourth EndFrame");
    CheckImmediate(immediate.GetCachedWidgetCount() == 0u,
        "container and descendants safely removed");

    // UIContextの描画Frame中はTree変更を許可しません。
    context.BeginFrame(Raven::math::Vec2(320.0f, 240.0f));
    CheckImmediate(immediate.BeginFrame() == false, "UIContext active rejects BeginFrame");
    context.EndFrame();

    // 入力通知はRetained Widgetに届き、次のImmediate宣言で一度だけ消費します。
    CheckImmediate(immediate.BeginFrame() == true, "widget API BeginFrame");
    auto* label = immediate.Text("caption", "Physics Debug");
    CheckImmediate(label != nullptr && label->GetText() == "Physics Debug",
        "Text creates label");
    CheckImmediate(immediate.Button("reset") == false, "button initially false");
    float gravity = 9.8f;
    CheckImmediate(immediate.SliderFloat("gravity", &gravity, 0.0f, 20.0f) == false,
        "slider initially unchanged");
    CheckImmediate(immediate.EndFrame() == true, "widget API EndFrame");

    auto* reset = dynamic_cast<Raven::UIButton*>(
        context.GetRootElement().GetChildren()[context.GetRootElement().GetChildren().size() - 2u].get());
    CheckImmediate(reset != nullptr && context.SetFocus(reset) == true,
        "focus immediate button");
    Raven::UIKeyEvent enter{};
    enter.Key = Raven::UIKey::Enter;
    enter.Pressed = true;
    CheckImmediate(context.RouteKeyEvent(enter) == true, "activate button");

    CheckImmediate(immediate.BeginFrame() == true, "consume input BeginFrame");
    CheckImmediate(immediate.Text("caption", "Updated") == label &&
        label->GetText() == "Updated", "label reused and updated");
    CheckImmediate(immediate.Button("reset") == true, "button click consumed");
    CheckImmediate(immediate.SliderFloat("gravity", &gravity, 0.0f, 20.0f) == false,
        "slider still unchanged");
    CheckImmediate(immediate.EndFrame() == true, "consume input EndFrame");

    CheckImmediate(immediate.BeginFrame() == true, "one-shot BeginFrame");
    CheckImmediate(immediate.Text("caption", "Updated") == label, "label remains stable");
    CheckImmediate(immediate.Button("reset") == false, "click not replayed");
    CheckImmediate(immediate.SliderFloat("gravity", &gravity, 0.0f, 20.0f) == false,
        "slider no replay");
    CheckImmediate(immediate.EndFrame() == true, "one-shot EndFrame");

    CheckImmediate(immediate.BeginFrame() == true, "abort BeginFrame");
    CheckImmediate(immediate.PushID("pending") == true, "abort PushID");
    immediate.AbortFrame();
    CheckImmediate(immediate.IsFrameActive() == false, "abort resets state");
    CheckImmediate(immediate.BeginFrame() == true, "restart after abort");
    CheckImmediate(immediate.EndFrame() == true, "restart EndFrame");
}

} // namespace
