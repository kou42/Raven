#pragma once

#include <algorithm>

namespace Raven
{

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

inline bool UIContext::SetFocus(UIElement* element)
{
    if (element != nullptr && (element->m_Context != this || element->IsFocusable() == false || element->IsVisible() == false))
    {
        return false;
    }

    // Focus対象をraw pointerとしてContextへ保持せず、Element自身の状態としてTree内に保持します。
    // これによりSubtree削除時はElementと一緒にFocus状態も破棄され、dangling pointerを作りません。
    std::vector<UIElement*> focusableElements;
    CollectFocusableElements(m_RootElement.get(), focusableElements);
    for (UIElement* candidate : focusableElements)
    {
        candidate->SetFocused(candidate == element);
    }
    return true;
}

inline void UIContext::ClearFocus()
{
    SetFocus(nullptr);
}

inline UIElement* UIContext::GetFocusedElement()
{
    std::vector<UIElement*> focusableElements;
    CollectFocusableElements(m_RootElement.get(), focusableElements);
    for (UIElement* candidate : focusableElements)
    {
        if (candidate->IsFocused() == true)
        {
            return candidate;
        }
    }
    return nullptr;
}

inline const UIElement* UIContext::GetFocusedElement() const
{
    return const_cast<UIContext*>(this)->GetFocusedElement();
}

inline bool UIContext::MoveFocus(bool reverse)
{
    std::vector<UIElement*> focusableElements;
    CollectFocusableElements(m_RootElement.get(), focusableElements);
    if (focusableElements.empty())
    {
        return false;
    }

    UIElement* focusedElement = GetFocusedElement();
    auto current = std::find(focusableElements.begin(), focusableElements.end(), focusedElement);
    std::size_t nextIndex = 0u;
    if (current != focusableElements.end())
    {
        const std::size_t currentIndex = static_cast<std::size_t>(std::distance(focusableElements.begin(), current));
        nextIndex = reverse == true
            ? (currentIndex == 0u ? focusableElements.size() - 1u : currentIndex - 1u)
            : (currentIndex + 1u) % focusableElements.size();
    }
    else if (reverse == true)
    {
        nextIndex = focusableElements.size() - 1u;
    }

    return SetFocus(focusableElements[nextIndex]);
}

} // namespace Raven
