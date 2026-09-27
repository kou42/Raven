#include "Raven/UI/Navigation/UIScreen.h"

#include <cassert>

namespace Raven
{

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
