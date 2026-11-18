#include "Raven/UI/Navigation/UIScreen.h"

#include "Raven/Core/Input.h"
#include "Raven/UI/Core/UIContext.h"

#include <cassert>

namespace Raven
{

void UIScreen::SetGamepadMenuPath(std::string path)
{
    m_GamepadMenuPath = std::move(path);
}

void UIScreen::OnPause()
{
    m_HasGamepadSample = false;
}

void UIScreen::UpdateGamepad(const GamepadState* state)
{
    auto& root = GetRootElement();
    auto* context = root.GetContext();
    auto* menu = m_GamepadMenuPath.empty() ? nullptr : root.FindByPath(m_GamepadMenuPath);
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

UIScreen::UIScreen()
    : m_RootElement(CreateScope<UIElement>())
{
}

UIElement& UIScreen::GetRootElement()
{
    UIElement* root = m_RootElement != nullptr ? m_RootElement.get() : m_AttachedRoot;
    assert(root != nullptr);
    return *root;
}

const UIElement& UIScreen::GetRootElement() const
{
    const UIElement* root = m_RootElement != nullptr ? m_RootElement.get() : m_AttachedRoot;
    assert(root != nullptr);
    return *root;
}

Scope<UIElement> UIScreen::ReleaseRootElement()
{
    return std::move(m_RootElement);
}

void UIScreen::RestoreRootElement(Scope<UIElement> root)
{
    m_AttachedRoot = nullptr;
    m_RootElement = std::move(root);
}

} // namespace Raven
