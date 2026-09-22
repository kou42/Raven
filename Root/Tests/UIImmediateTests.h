#pragma once

#include "Raven/UI/Immediate/UIImmediateContext.h"
#include "Raven/Physics/Debug/PhysicsDebugImmediatePanel.h"
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

    // Cacheからの再利用でも宣言順を描画順へ反映します。
    {
        Raven::UIContext orderContext;
        Raven::UIImmediateContext orderImmediate(orderContext);
        CheckImmediate(orderImmediate.BeginFrame() == true, "order first BeginFrame");
        auto* back = orderImmediate.GetOrCreate<Raven::UIButton>("back");
        auto* front = orderImmediate.GetOrCreate<Raven::UIButton>("front");
        CheckImmediate(orderImmediate.EndFrame() == true, "order first EndFrame");
        CheckImmediate(back != nullptr && front != nullptr, "order widgets created");
        CheckImmediate(orderImmediate.BeginFrame() == true, "order second BeginFrame");
        CheckImmediate(orderImmediate.GetOrCreate<Raven::UIButton>("front") == front,
            "front reused");
        CheckImmediate(orderImmediate.GetOrCreate<Raven::UIButton>("back") == back,
            "back reused");
        CheckImmediate(orderImmediate.EndFrame() == true, "order second EndFrame");
        const auto& ordered = orderContext.GetRootElement().GetChildren();
        CheckImmediate(ordered.size() == 2u && ordered[0].get() == front &&
            ordered[1].get() == back, "declaration order applied");
    }

    // Checkboxは外部boolを所有せず、クリックを次の宣言で一度だけ反映します。
    {
        Raven::UIContext checkContext;
        Raven::UIImmediateContext checkImmediate(checkContext);
        bool enabled = false;
        CheckImmediate(checkImmediate.BeginFrame() == true, "checkbox BeginFrame");
        CheckImmediate(checkImmediate.Checkbox("enabled", "Enabled", &enabled, nullptr) == false,
            "checkbox initial state");
        CheckImmediate(checkImmediate.EndFrame() == true, "checkbox EndFrame");
        auto* button = dynamic_cast<Raven::UIButton*>(
            checkContext.GetRootElement().GetChildren().front().get());
        CheckImmediate(button != nullptr && checkContext.SetFocus(button) == true,
            "checkbox focus");
        Raven::UIKeyEvent activate{};
        activate.Key = Raven::UIKey::Space;
        activate.Pressed = true;
        CheckImmediate(checkContext.RouteKeyEvent(activate) == true,
            "checkbox activate");
        CheckImmediate(checkImmediate.BeginFrame() == true, "checkbox consume BeginFrame");
        CheckImmediate(checkImmediate.Checkbox("enabled", "Enabled", &enabled, nullptr) == true &&
            enabled == true, "checkbox toggled");
        CheckImmediate(checkImmediate.EndFrame() == true, "checkbox consume EndFrame");
        CheckImmediate(checkImmediate.BeginFrame() == true, "checkbox one-shot BeginFrame");
        CheckImmediate(checkImmediate.Checkbox("enabled", "Enabled", &enabled, nullptr) == false &&
            enabled == true, "checkbox no replay");
        CheckImmediate(checkImmediate.EndFrame() == true, "checkbox one-shot EndFrame");
    }

    // Physics Debugは既存Rendererと同じSettingsを参照し、独立した状態を持ちません。
    {
        Raven::UIContext debugContext;
        Raven::UIImmediateContext debugImmediate(debugContext);
        Raven::ph::PhysicsDebugSettings settings{};
        CheckImmediate(debugImmediate.BeginFrame() == true, "debug panel BeginFrame");
        CheckImmediate(Raven::ph::DrawPhysicsDebugImmediatePanel(
            debugImmediate, settings, nullptr) == false, "debug panel initially unchanged");
        CheckImmediate(debugImmediate.EndFrame() == true, "debug panel EndFrame");
        CheckImmediate(debugImmediate.GetCachedWidgetCount() == 11u,
            "debug panel widgets cached");
        CheckImmediate(debugImmediate.BeginFrame() == true, "debug panel reuse BeginFrame");
        CheckImmediate(Raven::ph::DrawPhysicsDebugImmediatePanel(
            debugImmediate, settings, nullptr) == false, "debug panel reused");
        CheckImmediate(debugImmediate.EndFrame() == true, "debug panel reuse EndFrame");
        CheckImmediate(debugImmediate.GetCachedWidgetCount() == 11u,
            "debug panel widget count stable");
    }

    // UIContextの描画Frame中はTree変更を許可しません。
    context.BeginFrame(Raven::math::Vec2(320.0f, 240.0f));
    CheckImmediate(immediate.BeginFrame() == false, "UIContext active rejects BeginFrame");
    context.EndFrame();

    // 入力通知はRetained Widgetに届き、次のImmediate宣言で一度だけ消費します。
    CheckImmediate(immediate.BeginFrame() == true, "widget API BeginFrame");
    auto* label = immediate.Text("caption", "Physics Debug");
    CheckImmediate(label != nullptr && label->GetText() == "Physics Debug",
        "Text creates label");
    CheckImmediate(immediate.Button("reset", "Reset", nullptr) == false, "button initially false");
    float gravity = 9.8f;
    CheckImmediate(immediate.SliderFloat("gravity", &gravity, 0.0f, 20.0f) == false,
        "slider initially unchanged");
    CheckImmediate(immediate.EndFrame() == true, "widget API EndFrame");

    Raven::UIButton* reset = nullptr;
    for (const auto& element : context.GetRootElement().GetChildren())
    {
        if (auto* candidate = dynamic_cast<Raven::UIButton*>(element.get()))
        {
            reset = candidate;
            break;
        }
    }
    CheckImmediate(reset != nullptr && reset->GetChildren().size() == 1u,
        "button caption created");
    auto* caption = dynamic_cast<Raven::UILabel*>(reset->GetChildren().front().get());
    CheckImmediate(caption != nullptr && caption->GetText() == "Reset",
        "button caption text");
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

    Raven::UISlider* slider = nullptr;
    for (const auto& element : context.GetRootElement().GetChildren())
    {
        if (auto* candidate = dynamic_cast<Raven::UISlider*>(element.get()))
        {
            slider = candidate;
            break;
        }
    }
    CheckImmediate(slider != nullptr && context.SetFocus(slider) == true,
        "focus immediate slider");
    Raven::UIKeyEvent right{};
    right.Key = Raven::UIKey::Right;
    right.Pressed = true;
    CheckImmediate(context.RouteKeyEvent(right) == true, "adjust slider");

    CheckImmediate(immediate.BeginFrame() == true, "slider input BeginFrame");
    CheckImmediate(immediate.Text("caption", "Updated") == label, "label kept");
    CheckImmediate(immediate.Button("reset") == false, "button kept");
    CheckImmediate(immediate.SliderFloat("gravity", &gravity, 0.0f, 20.0f) == true,
        "slider input consumed");
    CheckImmediate(gravity > 9.8f, "slider writes caller value");
    CheckImmediate(immediate.EndFrame() == true, "slider input EndFrame");

    CheckImmediate(immediate.BeginFrame() == true, "panel BeginFrame");
    auto* panel = immediate.BeginPanel("debug", Raven::math::Vec2(12.0f, 16.0f),
        Raven::math::Vec2(220.0f, 160.0f));
    CheckImmediate(panel != nullptr, "panel created");
    CheckImmediate(immediate.Text("heading", "Debug") != nullptr, "panel text");
    CheckImmediate(immediate.Button("apply", "Apply", nullptr) == false,
        "panel button");
    CheckImmediate(immediate.EndContainer() == true, "panel EndContainer");
    CheckImmediate(immediate.EndFrame() == true, "panel EndFrame");
    CheckImmediate(panel->GetChildren().size() == 2u, "panel child count");

    std::string editText = "abc";
    CheckImmediate(immediate.BeginFrame() == true, "input BeginFrame");
    CheckImmediate(immediate.InputText("name", &editText) == false,
        "input initially unchanged");
    CheckImmediate(immediate.EndFrame() == true, "input EndFrame");
    Raven::UIInputText* input = nullptr;
    for (const auto& element : context.GetRootElement().GetChildren())
    {
        if (auto* candidate = dynamic_cast<Raven::UIInputText*>(element.get()))
        {
            input = candidate;
            break;
        }
    }
    CheckImmediate(input != nullptr && input->GetText() == "abc",
        "input initial text");
    CheckImmediate(context.SetFocus(input) == true, "focus input");
    CheckImmediate(context.RouteCharacterEvent(static_cast<std::uint32_t>('d')) == true,
        "type input character");
    CheckImmediate(immediate.BeginFrame() == true, "input consume BeginFrame");
    CheckImmediate(immediate.InputText("name", &editText) == true,
        "input changed");
    CheckImmediate(editText == input->GetText() && editText != "abc",
        "input writes caller string");
    CheckImmediate(immediate.EndFrame() == true, "input consume EndFrame");
    CheckImmediate(immediate.BeginFrame() == true, "input one-shot BeginFrame");
    CheckImmediate(immediate.InputText("name", &editText) == false,
        "input change not replayed");
    CheckImmediate(immediate.EndFrame() == true, "input one-shot EndFrame");

    CheckImmediate(immediate.BeginFrame() == true, "abort BeginFrame");
    CheckImmediate(immediate.PushID("pending") == true, "abort PushID");
    immediate.AbortFrame();
    CheckImmediate(immediate.IsFrameActive() == false, "abort resets state");
    CheckImmediate(immediate.BeginFrame() == true, "restart after abort");
    CheckImmediate(immediate.EndFrame() == true, "restart EndFrame");
}

} // namespace
