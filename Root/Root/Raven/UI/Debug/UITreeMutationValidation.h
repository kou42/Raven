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
    class MutationStateElement final : public UIElement
    {
    public:
        UIElement* Source = nullptr;
        UIElement* Destination = nullptr;
        UIElement* Target = nullptr;
        Scope<UIElement> Detached;

        void Detach()
        {
            if (Target == nullptr || Detached != nullptr)
            {
                return;
            }
            UIElement* parent = Target->GetParent();
            if (parent != nullptr)
            {
                Detached = parent->DetachChild(Target);
                std::cout << "Tree Mutation: detached" << std::endl;
            }
        }

        void AttachToSource()
        {
            Attach(Source, "source");
        }

        void AttachToDestination()
        {
            Attach(Destination, "destination");
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
                std::cout << "Tree Mutation: removed detached target" << std::endl;
                return;
            }
            UIElement* parent = Target->GetParent();
            if (parent != nullptr && parent->RemoveChild(Target) == true)
            {
                Target = nullptr;
                std::cout << "Tree Mutation: removed attached target" << std::endl;
            }
        }

    private:
        void Attach(UIElement* parent, const char* name)
        {
            if (parent == nullptr || Target == nullptr || Detached == nullptr)
            {
                return;
            }
            UIElement* attached = parent->AddChild(std::move(Detached));
            if (attached != nullptr)
            {
                Target = attached;
                std::cout << "Tree Mutation: attached to " << name << std::endl;
            }
        }
    };

    class CaptureMutationStateElement final : public UIElement
    {
    public:
        UIElement* Host = nullptr;
        UIElement* Subtree = nullptr;
        UISlider* Slider = nullptr;
        Scope<UIElement> Detached;
        bool Armed = true;

        void OnSliderValueChanged(float value)
        {
            if (Armed == false || Slider == nullptr || Subtree == nullptr || Detached != nullptr)
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
                return;
            }
            // MouseMove callback中にCapture ownerを含むSubtreeを外し、Cancelの同期cleanupを検証します。
            Armed = false;
            Detached = parent->DetachChild(Subtree);
            std::cout << "Tree Mutation capture stress: detached while dragging" << std::endl;
        }

        void Restore()
        {
            if (Host == nullptr || Subtree == nullptr || Slider == nullptr || Detached == nullptr)
            {
                return;
            }
            UIElement* attached = Host->AddChild(std::move(Detached));
            if (attached == nullptr)
            {
                return;
            }
            Subtree = attached;
            Slider->SetValue(0.10f);
            Armed = true;
            std::cout << "Tree Mutation capture stress: restored, dragging="
                << (Slider->IsDragging() == true ? "true" : "false") << std::endl;
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
    static Scope<UIElement> Create()
    {
        auto root = CreateScope<UIPanel>();
        root->SetPosition(math::Vec2(404.0f, 48.0f));
        root->SetSize(math::Vec2(420.0f, 372.0f));
        root->SetBackgroundColor(math::Vec4(0.05f, 0.08f, 0.14f, 0.96f));
        root->SetLayoutMode(UILayoutMode::Vertical);
        root->SetPadding(12.0f);
        root->SetSpacing(8.0f);

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
        target->SetOnClick([]()
            {
                std::cout << "Tree Mutation target clicked" << std::endl;
            });
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

        // State Elementは所有権保持だけに使い、Layout/Hit Testへ参加させません。
        auto state = CreateScope<MutationStateElement>();
        state->SetVisible(false);
        state->Source = sourceElement;
        state->Destination = destinationElement;
        state->Target = targetElement;
        MutationStateElement* stateElement = state.get();
        root->AddChild(std::move(state));
        auto controls = CreateScope<UIPanel>();
        controls->SetSize(math::Vec2(396.0f, 108.0f));
        controls->SetLayoutMode(UILayoutMode::Horizontal);
        controls->SetSpacing(6.0f);
        controls->AddChild(CreateControlButton(math::Vec4(0.52f, 0.32f, 0.12f, 1.0f),
            [stateElement]()
            {
                stateElement->Detach();
            }));
        controls->AddChild(CreateControlButton(math::Vec4(0.12f, 0.38f, 0.58f, 1.0f),
            [stateElement]()
            {
                stateElement->AttachToSource();
            }));
        controls->AddChild(CreateControlButton(math::Vec4(0.36f, 0.20f, 0.56f, 1.0f),
            [stateElement]()
            {
                stateElement->AttachToDestination();
            }));
        controls->AddChild(CreateControlButton(math::Vec4(0.58f, 0.16f, 0.18f, 1.0f),
            [stateElement]()
            {
                stateElement->Remove();
            }));
        root->AddChild(std::move(controls));

        // Sliderを右へDragして0.75へ到達すると、そのSliderを含むPanelをEvent callback内からDetachします。
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
        captureState->Host = captureHostElement;
        captureState->Subtree = captureSubtreeElement;
        captureState->Slider = captureSliderElement;
        CaptureMutationStateElement* captureStateElement = captureState.get();
        root->AddChild(std::move(captureState));
        captureSliderElement->SetOnValueChanged([captureStateElement](float value)
            {
                captureStateElement->OnSliderValueChanged(value);
            });
        auto resetButton = CreateControlButton(math::Vec4(0.16f, 0.42f, 0.46f, 1.0f),
            [captureStateElement]()
            {
                captureStateElement->Restore();
            });
        resetButton->SetSize(math::Vec2(112.0f, 120.0f));
        captureRow->AddChild(std::move(captureHost));
        captureRow->AddChild(std::move(resetButton));
        root->AddChild(std::move(captureRow));
        return root;
    }
};

} // namespace Raven
