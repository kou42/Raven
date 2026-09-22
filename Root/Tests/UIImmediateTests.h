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

    CheckImmediate(immediate.BeginFrame() == true, "abort BeginFrame");
    CheckImmediate(immediate.PushID("pending") == true, "abort PushID");
    immediate.AbortFrame();
    CheckImmediate(immediate.IsFrameActive() == false, "abort resets state");
    CheckImmediate(immediate.BeginFrame() == true, "restart after abort");
    CheckImmediate(immediate.EndFrame() == true, "restart EndFrame");
}

} // namespace
