#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{
bool Layer::OnUIKeyEvent(const UIKeyEvent& event)
{
    static_cast<void>(event);
    return false;
}
} // namespace Raven
