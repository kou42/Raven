#pragma once

#include "Raven/UI/Widgets/UIScrollView.h"

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

    std::vector<UIElement*> focusableElements;
    CollectFocusableElements(m_RootElement.get(), focusableElements);
    for (UIElement* candidate : focusableElements)
    {
        candidate->SetFocused(candidate == element);
    }

    if (element != nullptr)
    {
        // Focus対象からRootへ向かって祖先ScrollViewを順に処理します。
        // 内側を先にEnsureVisibleすることで、Nested ScrollViewでも各Viewportが必要最小限だけ追従します。
        UIElement* current = element->GetParent();
        while (current != nullptr)
        {
            UIScrollView* scrollView = dynamic_cast<UIScrollView*>(current);
            if (scrollView != nullptr)
            {
                scrollView->EnsureVisible(element);
            }
            current = current->GetParent();
        }
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

inline bool UIContext::RouteKeyEvent(const UIKeyEvent& event)
{
    if (event.Pressed == true && event.Repeat == false && event.Key == UIKey::Tab)
    {
        return MoveFocus(event.Shift);
    }

    UIElement* focusedElement = GetFocusedElement();
    if (focusedElement == nullptr)
    {
        return false;
    }

    // Tab以外のKeyboard入力は現在Focusを持つWidgetへ直接配送します。
    // MouseのようなHit Testは不要で、Focus所有者だけが操作を消費するため親への暗黙Bubbleも行いません。
    UIKeyEvent routedEvent = event;
    routedEvent.Context = this;
    focusedElement->HandleKeyEvent(routedEvent);
    return routedEvent.Handled;
}

} // namespace Raven
