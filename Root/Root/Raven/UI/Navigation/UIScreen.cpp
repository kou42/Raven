#include "Raven/UI/Navigation/UIScreen.h"

namespace Raven
{

UIScreen::UIScreen()
    : m_RootElement(CreateScope<UIElement>())
{
}

Scope<UIElement> UIScreen::ReleaseRootElement()
{
    return std::move(m_RootElement);
}

void UIScreen::RestoreRootElement(Scope<UIElement> root)
{
    m_RootElement = std::move(root);
}

} // namespace Raven
