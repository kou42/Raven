#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UISlider.h"

#include <iostream>

namespace Raven
{

// Retained Treeの所有権変更とInteraction cleanupを、実Mouse Eventから再現するDebug専用検証UIです。
class UITreeMutationValidation
{
private:
    class ValidationResultState final : public UIElement
    {
    public:
        UIPanel* Indicator = nullptr;
        int PassedCount = 0;
        int FailedCount = 0;

        void Record(const char* name, bool passed)
        {
            if (passed == true)
            {
                ++PassedCount;
                std::cout << "[UI Validation][PASS] " << name << std::endl;
            }
            else
            {
                ++FailedCount;
                std::cout << "[UI Validation][FAIL] " << name << std::endl;
            }

            if (Indicator != nullptr)
            {
                Indicator->SetBackgroundColor(FailedCount == 0
                    ? math::Vec4(0.10f, 0.34f, 0.18f, 1.0f)
                    : math::Vec4(0.52f, 0.10f, 0.12f, 1.0f));
            }

            std::cout << "[UI Validation] total: pass=" << PassedCount
                << ", fail=" << FailedCount << std::endl;
        }
    };

    class MutationStateElement final : public UIElement
    {
    public:
        UIContext* Context = nullptr;
        UIElement* Source = nullptr;
        UIElement* Destination = nullptr;
        UIElement* Target = nullptr;
        ValidationResultState* Results = nullptr;
        Scope<UIElement> Detached;

        void Detach()
        {
            if (Context == nullptr || Target == nullptr || Detached != nullptr)
            {
                return;
            }

            UIElement* previousParent = Target->GetParent();
            if (previousParent == nullptr)
            {
                Record("Detach precondition: target has parent", false);
                return;
            }

            Detached = previousParent->DetachChild(Target);
            Record("Detach returns ownership", Detached != nullptr);
            Record("Detach clears parent", Target->GetParent() == nullptr);
            Record("Detach clears hovered flag", Target->IsHovered() == false);
            Record("Detach clears pressed flag", Target->IsPressed() == false);
            Record("Detach clears context hover target", Context->GetHoveredElement() != Target);
            Record("Detach clears context pressed target", Context->GetPressedElement() != Target);
        }

        void AttachToSource()
        {
            Attach(Source, "Attach source parent");
        }

        void AttachToDestination()
        {
            Attach(Destination, "Attach destination parent");
        }

        void Remove()
        {
            if (Target == nullptr)
            {
                return;
            }

            if (Detached != nullptr)
            {
                Detached.reset();
                Target = nullptr;
                Record("Remove detached target releases ownership", Detached == nullptr);
                return;
            }

            UIElement* parent = Target->GetParent();
            if (parent == nullptr)
            {
                Record("Remove precondition: attached target has parent", false);
                return;
            }

            const bool removed = parent->RemoveChild(Target);
            if (removed == true)
            {
                Target = nullptr;
            }
            Record("Remove attached target succeeds", removed == true);
            Record("Remove clears borrowed target", Target == nullptr);
        }

    private:
        void Attach(UIElement* parent, const char* resultName)
        {
            if (parent == nullptr || Target == nullptr || Detached == nullptr)
            {
                return;
            }

            UIElement* attached = parent->AddChild(std::move(Detached));
            Record("Attach transfers ownership to tree", attached != nullptr && Detached == nullptr);
            if (attached == nullptr)
            {
                // AddChild()はScopeを値で受け取るため失敗時も要素は破棄されます。
                // 以降dangling pointerを残さないよう借用参照を無効化します。
                Target = nullptr;
                return;
            }

            Target = attached;
            Record(resultName, Target->GetParent() == parent);
        }

        void Record(const char* name, bool passed)
        {
            if (Results != nullptr)
            {
                Results->Record(name, passed);
            }
        }
    };

    class CaptureMutationStateElement final : public UIElement
    {
    public:
        UIContext* Context = nullptr;
        UIElement* Host = nullptr;
        UIElement* Subtree = nullptr;
        UISlider* Slider = nullptr;
        ValidationResultState* Results = nullptr;
        Scope<UIElement> Detached;
        bool Armed = true;

        void OnSliderValueChanged(float value)
        {
            if (Armed == false || Context == nullptr || Slider == nullptr || Subtree == nullptr || Detached != nullptr)
            {
                return;
            }
            if (value < 0.75f || Slider->IsDragging() == false)
            {
                return;
            }

            UIElement* parent = Subtree->GetParent();
            if (parent == nullptr)
            {
                Record("Capture mutation precondition: subtree has parent", false);
                return;
            }

            Record("Capture exists before subtree detach", Context->HasMouseCapture(Slider) == true);
            Armed = false;
            Detached = parent->DetachChild(Subtree);

            // DetachChild()はParent chainが有効な間にUIContext::OnSubtreeRemoving()を呼ぶため、
            // このcallbackから戻る前にCapture / Pressed / Draggingが全て解除されている必要があります。
            Record("Capture subtree detach returns ownership", Detached != nullptr);
            Record("Capture subtree detach clears parent", Subtree->GetParent() == nullptr);
            Record("Capture subtree detach releases capture", Context->HasMouseCapture() == false);
            Record("Capture subtree detach clears dragging", Slider->IsDragging() == false);
            Record("Capture subtree detach clears pressed", Slider->IsPressed() == false);
            Record("Capture subtree detach clears context pressed target", Context->GetPressedElement() != Slider);
            Record("Capture subtree detach clears context hover target", Context->GetHoveredElement() != Slider);
        }

        void Restore()
        {
            if (Host == nullptr || Subtree == nullptr || Slider == nullptr || Detached == nullptr)
            {
                return;
            }

            UIElement* attached = Host->AddChild(std::move(Detached));
            Record("Capture subtree restore transfers ownership", attached != nullptr && Detached == nullptr);
            if (attached == nullptr)
            {
                Subtree = nullptr;
                Slider = nullptr;
                return;
            }

            Subtree = attached;
            Record("Capture subtree restore parent", Subtree->GetParent() == Host);
            Slider->SetValue(0.10f);
            Armed = true;
            Record("Capture subtree restore keeps dragging cleared", Slider->IsDragging() == false);
        }

    private:
        void Record(const char* name, bool passed)
        {
            if (Results != nullptr)
            {
                Results->Record(name, passed);
            }
        }
    };

    static Scope<UIButton> CreateControlButton(const math::Vec4& color, UIButton::ClickHandler handler)
    {
        auto button = CreateScope<UIButton>();
        button->SetSize(math::Vec2(94.0f, 108.0f));
        button->SetNormalColor(color);
        button->SetHoveredColor(math::Vec4(color.x + 0.08f, color.y + 0.08f, color.z + 0.08f, color.w));
        button->SetPressedColor(math::Vec4(color.x * 0.72f, color.y * 0.72f, color.z * 0.72f, color.w));
        button->SetOnClick(std::move(handler));
        return button;
    }

public:
    static Scope<UIElement> Create(UIContext& context)
    {
        auto root = CreateScope<UIPanel>();
        root->SetPosition(math::Vec2(404.0f, 48.0f));
        root->SetSize(math::Vec2(420.0f, 396.0f));
        root->SetBackgroundColor(math::Vec4(0.05f, 0.08f, 0.14f, 0.96f));
        root->SetLayoutMode(UILayoutMode::Vertical);
        root->SetPadding(12.0f);
        root->SetSpacing(8.0f);

        // Text描画Widgetが未実装のため、Result indicatorは色で集約結果を示します。
        // 緑=全PASS、赤=1件以上FAIL。個別結果と件数はConsoleへ出力します。
        auto resultIndicator = CreateScope<UIPanel>();
        resultIndicator->SetSize(math::Vec2(396.0f, 16.0f));
        resultIndicator->SetBackgroundColor(math::Vec4(0.18f, 0.20f, 0.26f, 1.0f));
        UIPanel* resultIndicatorElement = resultIndicator.get();
        root->AddChild(std::move(resultIndicator));

        auto results = CreateScope<ValidationResultState>();
        results->SetVisible(false);
        results->Indicator = resultIndicatorElement;
        ValidationResultState* resultsElement = results.get();
        root->AddChild(std::move(results));

        auto mutationRow = CreateScope<UIPanel>();
        mutationRow->SetSize(math::Vec2(396.0f, 104.0f));
        mutationRow->SetLayoutMode(UILayoutMode::Horizontal);
        mutationRow->SetSpacing(8.0f);
        auto sourcePanel = CreateScope<UIPanel>();
        sourcePanel->SetSize(math::Vec2(194.0f, 104.0f));
        sourcePanel->SetBackgroundColor(math::Vec4(0.12f, 0.24f, 0.18f, 1.0f));
        sourcePanel->SetLayoutMode(UILayoutMode::Vertical);
        sourcePanel->SetPadding(8.0f);
        UIElement* sourceElement = sourcePanel.get();
        auto target = CreateScope<UIButton>();
        target->SetSize(math::Vec2(178.0f, 88.0f));
        target->SetNormalColor(math::Vec4(0.16f, 0.50f, 0.30f, 1.0f));
        target->SetHoveredColor(math::Vec4(0.22f, 0.66f, 0.40f, 1.0f));
        target->SetPressedColor(math::Vec4(0.10f, 0.34f, 0.20f, 1.0f));
        UIElement* targetElement = target.get();
        sourcePanel->AddChild(std::move(target));
        auto destinationPanel = CreateScope<UIPanel>();
        destinationPanel->SetSize(math::Vec2(194.0f, 104.0f));
        destinationPanel->SetBackgroundColor(math::Vec4(0.22f, 0.14f, 0.30f, 1.0f));
        destinationPanel->SetLayoutMode(UILayoutMode::Vertical);
        destinationPanel->SetPadding(8.0f);
        UIElement* destinationElement = destinationPanel.get();
        mutationRow->AddChild(std::move(sourcePanel));
        mutationRow->AddChild(std::move(destinationPanel));
        root->AddChild(std::move(mutationRow));

        auto state = CreateScope<MutationStateElement>();
        state->SetVisible(false);
        state->Context = &context;
        state->Source = sourceElement;
        state->Destination = destinationElement;
        state->Target = targetElement;
        state->Results = resultsElement;
        MutationStateElement* stateElement = state.get();
        root->AddChild(std::move(state));
        auto controls = CreateScope<UIPanel>();
        controls->SetSize(math::Vec2(396.0f, 108.0f));
        controls->SetLayoutMode(UILayoutMode::Horizontal);
        controls->SetSpacing(6.0f);
        controls->AddChild(CreateControlButton(math::Vec4(0.52f, 0.32f, 0.12f, 1.0f), [stateElement]() { stateElement->Detach(); }));
        controls->AddChild(CreateControlButton(math::Vec4(0.12f, 0.38f, 0.58f, 1.0f), [stateElement]() { stateElement->AttachToSource(); }));
        controls->AddChild(CreateControlButton(math::Vec4(0.36f, 0.20f, 0.56f, 1.0f), [stateElement]() { stateElement->AttachToDestination(); }));
        controls->AddChild(CreateControlButton(math::Vec4(0.58f, 0.16f, 0.18f, 1.0f), [stateElement]() { stateElement->Remove(); }));
        root->AddChild(std::move(controls));

        auto captureRow = CreateScope<UIPanel>();
        captureRow->SetSize(math::Vec2(396.0f, 120.0f));
        captureRow->SetLayoutMode(UILayoutMode::Horizontal);
        captureRow->SetSpacing(8.0f);
        auto captureHost = CreateScope<UIPanel>();
        captureHost->SetSize(math::Vec2(276.0f, 120.0f));
        captureHost->SetBackgroundColor(math::Vec4(0.10f, 0.18f, 0.28f, 1.0f));
        captureHost->SetLayoutMode(UILayoutMode::Vertical);
        captureHost->SetPadding(10.0f);
        UIElement* captureHostElement = captureHost.get();
        auto captureSubtree = CreateScope<UIPanel>();
        captureSubtree->SetSize(math::Vec2(256.0f, 100.0f));
        captureSubtree->SetBackgroundColor(math::Vec4(0.12f, 0.26f, 0.36f, 1.0f));
        captureSubtree->SetLayoutMode(UILayoutMode::Vertical);
        captureSubtree->SetPadding(10.0f);
        UIElement* captureSubtreeElement = captureSubtree.get();
        auto captureSlider = CreateScope<UISlider>();
        captureSlider->SetSize(math::Vec2(236.0f, 80.0f));
        captureSlider->SetRange(0.0f, 1.0f);
        captureSlider->SetValue(0.10f);
        UISlider* captureSliderElement = captureSlider.get();
        captureSubtree->AddChild(std::move(captureSlider));
        captureHost->AddChild(std::move(captureSubtree));
        auto captureState = CreateScope<CaptureMutationStateElement>();
        captureState->SetVisible(false);
        captureState->Context = &context;
        captureState->Host = captureHostElement;
        captureState->Subtree = captureSubtreeElement;
        captureState->Slider = captureSliderElement;
        captureState->Results = resultsElement;
        CaptureMutationStateElement* captureStateElement = captureState.get();
        root->AddChild(std::move(captureState));
        captureSliderElement->SetOnValueChanged([captureStateElement](float value)
            {
                captureStateElement->OnSliderValueChanged(value);
            });
        auto resetButton = CreateControlButton(math::Vec4(0.16f, 0.42f, 0.46f, 1.0f), [captureStateElement]() { captureStateElement->Restore(); });
        resetButton->SetSize(math::Vec2(112.0f, 120.0f));
        captureRow->AddChild(std::move(captureHost));
        captureRow->AddChild(std::move(resetButton));
        root->AddChild(std::move(captureRow));
        return root;
    }
};

} // namespace Raven
