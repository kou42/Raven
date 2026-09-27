#pragma once

#include "Raven/UI/Navigation/UIScreen.h"

#include <functional>

namespace Raven
{

// Title / Pauseの双方から再利用するSettings画面の最小基盤です。
// 設定項目自体は後続Phaseで追加し、ここではScreen StackのBack経路を確立します。
class SettingsScreen final : public UIScreen
{
public:
    using Action = std::function<void()>;

    explicit SettingsScreen(Action onBack);
};

} // namespace Raven
