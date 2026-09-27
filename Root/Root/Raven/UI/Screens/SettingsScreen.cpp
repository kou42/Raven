#include "Raven/UI/Screens/SettingsScreen.h"

#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIPanel.h"

#include <utility>

namespace Raven
{

SettingsScreen::SettingsScreen(Action onBack)
{
    UIElement& root = GetRootElement();
    root.SetLayoutMode(UILayoutMode::Absolute);

    auto panel = CreateScope<UIPanel>();
    panel->SetPositionDIP(math::Vec2(48.0f, 96.0f));
    panel->SetPreferredSizeDIP(math::Vec2(320.0f, 112.0f));
    panel->SetLayoutMode(UILayoutMode::Vertical);
    panel->SetPaddingDIP(UIThickness(16.0f));
    panel->SetBackgroundColor(math::Vec4(0.04f, 0.06f, 0.10f, 0.96f));

    auto backButton = CreateScope<UIButton>();
    backButton->SetPreferredSizeDIP(math::Vec2(288.0f, 56.0f));
    backButton->SetFocusable(true);
    backButton->SetNormalColor(math::Vec4(0.22f, 0.18f, 0.30f, 1.0f));
    backButton->SetHoveredColor(math::Vec4(0.34f, 0.27f, 0.46f, 1.0f));
    backButton->SetPressedColor(math::Vec4(0.15f, 0.12f, 0.22f, 1.0f));
    backButton->SetFocusedColor(math::Vec4(0.48f, 0.38f, 0.64f, 1.0f));
    backButton->SetOnClick(std::move(onBack));
    panel->AddChild(std::move(backButton));

    AddChild(std::move(panel));
}

} // namespace Raven
