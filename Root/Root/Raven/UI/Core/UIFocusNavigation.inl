#pragma once

#include <algorithm>

namespace Raven
{

inline bool UIContext::SetFocus(UIElement* element)
{
    if (element == nullptr)
    {
        ClearFocus();
        return true;
    }
    if (element->m_Context != this || element->IsFocusable() == false || element->IsVisible() == false)
    {
        return false;
    }
    if (m_FocusedElement == element)
    {
        return true;
    }
    if (m_FocusedElement != nullptr)
    {
        m_FocusedElement->SetFocused(false);
    }
    m_FocusedElement = element;
    m_FocusedElement->SetFocused(true);
    return true;
}

inline void UIContext::ClearFocus()
{
    if (m_FocusedElement != nullptr)
    {
        m_FocusedElement->SetFocused(false);
        m_FocusedElement = nullptr;
    }
}

inline UIElement* UIContext::GetFocusedElement() { return m_FocusedElement; }
inline const UIElement* UIContext::GetFocusedElement() const { return m_FocusedElement; }

inline void UIContext::CollectFocusableElements(UIElement* root, std::vector<UIElement*>& outElements) const
{
    if (root == nullptr || root->IsVisible() == false)
    {
        return;
    }
    if (root->IsFocusable() == true)
    {
        outElements.push_back(root);
    }
    for (const auto& child : root->GetChildren())
    {
        if (child != nullptr)
        {
            CollectFocusableElements(child.get(), outElements);
        }
    }
}

inline bool UIContext::MoveFocus(bool reverse)
{
    std::vector<UIElement*> focusableElements;
    CollectFocusableElements(m_RootElement.get(), focusableElements);
    if (focusableElements.empty())
    {
        ClearFocus();
        return false;
    }

    auto current = std::find(focusableElements.begin(), focusableElements.end(), m_FocusedElement);
    std::size_t nextIndex = 0u;
    if (current != focusableElements.end())
    {
        const std::size_t currentIndex = static_cast<std::size_t>(std::distance(focusableElements.begin(), current));
        if (reverse == true)
        {
            nextIndex = currentIndex == 0u ? focusableElements.size() - 1u : currentIndex - 1u;
        }
        else
        {
            nextIndex = (currentIndex + 1u) % focusableElements.size();
        }
    }
    else if (reverse == true)
    {
        nextIndex = focusableElements.size() - 1u;
    }

    return SetFocus(focusableElements[nextIndex]);
}

} // namespace Raven
