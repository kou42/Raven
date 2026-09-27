#pragma once

#include "Raven/UI/Navigation/UIScreen.h"

namespace Raven
{

// Runtime Game UIの常駐レイヤーです。
// 現段階ではHUD用Rootだけを提供し、Gameplay情報Widgetは後続機能から追加します。
class HUDScreen final : public UIScreen
{
public:
    HUDScreen();
};

} // namespace Raven
