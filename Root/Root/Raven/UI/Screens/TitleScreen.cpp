#include "Raven/UI/Screens/TitleScreen.h"

#include "Raven/Core/Input.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UILabel.h"

#include <utility>

namespace Raven
{

TitleScreen::TitleScreen(const Ref<UIFontAtlas>& font, Action onStartGame, Action onSettings, Action onExit)
{
    UIElement& root = GetRootElement();
    root.SetName("TitleScreen");
    root.SetLayoutMode(UILayoutMode::Absolute);
    root.SetHitTestVisible(true);

    // Title UIはScene描画から独立したRetained Treeとして構築します。
    // 現在のUIButtonはTextを所有しないため、Font Asset統合前でもAction経路を検証できる
    // 最小の3Button Menuを先行し、文字表示はLoading/Runtime UIのFont統合時に追加します。
    auto menu = CreateScope<UIPanel>();
    menu->SetName("Menu");
    menu->SetPositionDIP(math::Vec2(48.0f, 96.0f));
    menu->SetPreferredSizeDIP(math::Vec2(320.0f, 224.0f));
    menu->SetLayoutMode(UILayoutMode::Vertical);
    menu->SetPaddingDIP(UIThickness(16.0f));
    menu->SetSpacingDIP(12.0f);
    menu->SetBackgroundColor(math::Vec4(0.04f, 0.06f, 0.10f, 0.94f));

    menu->AddChild(CreateMenuButton(font, "StartGameButton", "Start Game", std::move(onStartGame)));
    menu->AddChild(CreateMenuButton(font, "SettingsButton", "Settings", std::move(onSettings)));
    menu->AddChild(CreateMenuButton(font, "ExitButton", "Exit", std::move(onExit)));

    AddChild(std::move(menu));
}

void TitleScreen::OnPause()
{
    m_HasGamepadSample = false;
}

void TitleScreen::UpdateGamepad(const GamepadState* state)
{
    auto& root = GetRootElement();
    auto* context = root.GetContext();
    auto* menu = root.FindByPath("Menu");
    if (state == nullptr || context == nullptr || menu == nullptr ||
        menu->GetChildren().empty() || context->GetOpenPopup() != nullptr)
    {
        m_HasGamepadSample = false;
        return;
    }

    // 初回・再接続・画面復帰では現在値を基準にし、保持中の入力で遷移しません。
    const int direction = state->LeftStickY > 0.6f ? -1 : (state->LeftStickY < -0.6f ? 1 : 0);
    const bool navigate = m_HasGamepadSample && direction != 0 && direction != m_Direction;
    const bool activate = m_HasGamepadSample && state->ButtonA && m_ConfirmHeld == false;
    m_HasGamepadSample = true;
    m_Direction = direction;
    m_ConfirmHeld = state->ButtonA;

    const auto& buttons = menu->GetChildren();
    std::size_t index = 0u;
    while (index < buttons.size() && buttons[index].get() != context->GetFocusedElement())
    {
        ++index;
    }
    if (navigate == true || (activate == true && index == buttons.size()))
    {
        if (index == buttons.size())
        {
            index = direction < 0 ? buttons.size() - 1u : 0u;
        }
        else
        {
            index = direction < 0 ? (index + buttons.size() - 1u) % buttons.size()
                : (index + 1u) % buttons.size();
        }
        context->SetFocus(buttons[index].get());
        return;
    }
    if (activate == true && index < buttons.size())
    {
        UIKeyEvent event{};
        event.Key = UIKey::Enter;
        event.Pressed = true;
        context->RouteKeyEvent(event);
    }
}

Scope<UIElement> TitleScreen::CreateMenuButton(const Ref<UIFontAtlas>& font, const char* name, const char* text, Action action)
{
    auto button = CreateScope<UIButton>();
    button->SetName(name);
    button->SetPreferredSizeDIP(math::Vec2(288.0f, 56.0f));
    button->SetFocusable(true);
    button->SetNormalColor(math::Vec4(0.12f, 0.20f, 0.34f, 1.0f));
    button->SetHoveredColor(math::Vec4(0.18f, 0.32f, 0.52f, 1.0f));
    button->SetPressedColor(math::Vec4(0.08f, 0.14f, 0.25f, 1.0f));
    button->SetFocusedColor(math::Vec4(0.28f, 0.48f, 0.76f, 1.0f));
    button->SetOnClick(std::move(action));
    if (font != nullptr)
    {
        button->SetLayoutMode(UILayoutMode::Absolute);
        auto label = CreateScope<UILabel>();
        label->SetFont(font);
        label->SetText(text);
        label->SetPositionDIP(math::Vec2(16.0f, 16.0f));
        label->SetPreferredSizeDIP(math::Vec2(256.0f, 24.0f));
        label->SetLineHeightDIP(24.0f);
        label->SetBaselineOffsetDIP(18.0f);
        label->SetHitTestVisible(false);
        button->AddChild(std::move(label));
    }
    return button;
}

} // namespace Raven
