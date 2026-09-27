#pragma once

#include "Raven/UI/Navigation/UIScreen.h"

#include <functional>

namespace Raven
{

// Title Scene上に表示するRuntime UIです。
// Scene遷移やApplication終了の具体処理は持たず、Action Callbackだけを通知することで
// UI層からScene/ApplicationのLifetime制御を分離します。
class TitleScreen final : public UIScreen
{
public:
    using Action = std::function<void()>;

    TitleScreen(Action onStartGame, Action onSettings, Action onExit);

private:
    static Scope<UIElement> CreateMenuButton(Action action);
};

} // namespace Raven
