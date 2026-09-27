#include "Raven/UI/Screens/HUDScreen.h"

namespace Raven
{

HUDScreen::HUDScreen()
{
    UIElement& root = GetRootElement();
    root.SetName("HUDScreen");
    root.SetLayoutMode(UILayoutMode::Absolute);

    // HUD自身に背景Hit領域を持たせず、World操作を遮らない常駐Layerとして扱います。
    root.SetHitTestVisible(false);
}

} // namespace Raven
