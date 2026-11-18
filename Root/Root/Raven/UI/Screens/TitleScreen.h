#pragma once

#include "Raven/UI/Navigation/UIScreen.h"

#include <functional>

namespace Raven
{

class UIFontAtlas;
struct GamepadState;

// Title Scene上に表示するRuntime UIです。
// Scene遷移やApplication終了の具体処理は持たず、Action Callbackだけを通知することで
// UI層からScene/ApplicationのLifetime制御を分離します。
class TitleScreen final : public UIScreen
{
public:
    using Action = std::function<void()>;

    TitleScreen(const Ref<UIFontAtlas>& font, Action onStartGame, Action onSettings, Action onExit);

    void UpdateGamepad(const GamepadState* state);
    void OnPause() override;

private:
    bool m_HasGamepadSample = false;
    bool m_ConfirmHeld = false;
    int m_Direction = 0;

    static Scope<UIElement> CreateMenuButton(const Ref<UIFontAtlas>& font, const char* name, const char* text, Action action);
};

} // namespace Raven
