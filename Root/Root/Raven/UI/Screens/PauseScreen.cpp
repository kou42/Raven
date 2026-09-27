#include "Raven/UI/Screens/PauseScreen.h"

#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIPanel.h"

#include <utility>

namespace Raven
{

PauseScreen::PauseScreen(Action onResume, Action onSettings, Action onReturnToTitle)
{
    UIElement& root = GetRootElement();
    root.SetName("PauseScreen");
    root.SetLayoutMode(UILayoutMode::Absolute);
    root.SetHitTestVisible(true);

    auto menu = CreateScope<UIPanel>();
    menu->SetName("PauseMenu");
    menu->SetPositionDIP(math::Vec2(48.0f, 96.0f));
    menu->SetPreferredSizeDIP(math::Vec2(320.0f, 224.0f));
    menu->SetLayoutMode(UILayoutMode::Vertical);
    menu->SetPaddingDIP(UIThickness(16.0f));
    menu->SetSpacingDIP(12.0f);
    menu->SetBackgroundColor(math::Vec4(0.05f, 0.05f, 0.08f, 0.96f));

    menu->AddChild(CreateMenuButton("ResumeButton", std::move(onResume)));
    menu->AddChild(CreateMenuButton("SettingsButton", std::move(onSettings)));
    menu->AddChild(CreateMenuButton("ReturnToTitleButton", std::move(onReturnToTitle)));
    AddChild(std::move(menu));
}

Scope<UIElement> PauseScreen::CreateMenuButton(const char* name, Action action)
{
    auto button = CreateScope<UIButton>();
    button->SetName(name);
    button->SetPreferredSizeDIP(math::Vec2(288.0f, 56.0f));
    button->SetFocusable(true);
    button->SetNormalColor(math::Vec4(0.16f, 0.16f, 0.24f, 1.0f));
    button->SetHoveredColor(math::Vec4(0.26f, 0.26f, 0.40f, 1.0f));
    button->SetPressedColor(math::Vec4(0.10f, 0.10f, 0.18f, 1.0f));
    button->SetFocusedColor(math::Vec4(0.42f, 0.42f, 0.66f, 1.0f));
    button->SetOnClick(std::move(action));
    return button;
}

} // namespace Raven
