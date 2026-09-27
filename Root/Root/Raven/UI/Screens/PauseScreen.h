#pragma once

#include "Raven/UI/Navigation/UIScreen.h"

#include <functional>

namespace Raven
{

// Game Scene上へ積むPause Menuです。
// Scene/Application操作はActionとして外から注入し、Screen自身はNavigation方針を持ちません。
class PauseScreen final : public UIScreen
{
public:
    using Action = std::function<void()>;

    PauseScreen(Action onResume, Action onSettings, Action onReturnToTitle);

private:
    static Scope<UIElement> CreateMenuButton(const char* name, Action action);
};

} // namespace Raven
