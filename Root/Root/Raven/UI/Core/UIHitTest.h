#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

// ============================================================================
// UIHitTest
// ============================================================================
// Retained Mode UI Treeから、指定した画面座標の最前面Elementを検索します。
//
// UIElementの描画はParent -> ChildのPainter's Orderで行われるため、Hit Testでは
// Childを逆順に探索します。これにより、後から描画されて見た目上手前にあるElementが
// 入力についても優先され、描画順と入力順が一致します。
class UIHitTest final
{
public:
    UIHitTest() = delete;

    static UIElement* FindTopmost(UIElement& root, const math::Vec2& screenPosition)
    {
        return FindTopmostRecursive(root, screenPosition, math::Vec2(0.0f, 0.0f));
    }

    static const UIElement* FindTopmost(const UIElement& root, const math::Vec2& screenPosition)
    {
        return FindTopmostRecursive(root, screenPosition, math::Vec2(0.0f, 0.0f));
    }

private:
    static bool ContainsPoint(
        const math::Vec2& absolutePosition,
        const math::Vec2& size,
        const math::Vec2& screenPosition)
    {
        // 左上を含み右下を含まない半開区間に統一します。
        // 隣接するElementの境界上で2つが同時にHitする曖昧さを避けるためです。
        return screenPosition.x >= absolutePosition.x &&
            screenPosition.y >= absolutePosition.y &&
            screenPosition.x < absolutePosition.x + size.x &&
            screenPosition.y < absolutePosition.y + size.y;
    }

    static UIElement* FindTopmostRecursive(
        UIElement& element,
        const math::Vec2& screenPosition,
        const math::Vec2& parentAbsolutePosition)
    {
        if (element.IsVisible() == false)
        {
            return nullptr;
        }

        const math::Vec2& localPosition = element.GetPosition();
        const math::Vec2 absolutePosition(
            parentAbsolutePosition.x + localPosition.x,
            parentAbsolutePosition.y + localPosition.y);

        const auto& children = element.GetChildren();

        // BuildDrawListRecursive()とは逆順に探索します。
        // 現在のRaven UIはZIndexを持たないため、Childの登録順がそのままPainter's Orderです。
        for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator)
        {
            if (*iterator == nullptr)
            {
                continue;
            }

            UIElement* hit = FindTopmostRecursive(
                *(*iterator),
                screenPosition,
                absolutePosition);

            if (hit != nullptr)
            {
                return hit;
            }
        }

        if (ContainsPoint(absolutePosition, element.GetSize(), screenPosition) == true)
        {
            return &element;
        }

        return nullptr;
    }

    static const UIElement* FindTopmostRecursive(
        const UIElement& element,
        const math::Vec2& screenPosition,
        const math::Vec2& parentAbsolutePosition)
    {
        if (element.IsVisible() == false)
        {
            return nullptr;
        }

        const math::Vec2& localPosition = element.GetPosition();
        const math::Vec2 absolutePosition(
            parentAbsolutePosition.x + localPosition.x,
            parentAbsolutePosition.y + localPosition.y);

        const auto& children = element.GetChildren();

        for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator)
        {
            if (*iterator == nullptr)
            {
                continue;
            }

            const UIElement* hit = FindTopmostRecursive(
                *(*iterator),
                screenPosition,
                absolutePosition);

            if (hit != nullptr)
            {
                return hit;
            }
        }

        if (ContainsPoint(absolutePosition, element.GetSize(), screenPosition) == true)
        {
            return &element;
        }

        return nullptr;
    }
};

} // namespace Raven
