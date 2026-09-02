#pragma once

#include "Raven/Math/MathVector.h"

namespace Raven
{

class UIElement;
struct UITransform2D;

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

    static UIElement* FindTopmost(UIElement& root, const math::Vec2& screenPosition);
    static const UIElement* FindTopmost(const UIElement& root, const math::Vec2& screenPosition);

private:
    static bool ContainsPoint(
        const UIElement& element,
        const math::Vec2& absolutePosition,
        const UITransform2D& worldTransform,
        const math::Vec2& screenPosition);

    static UIElement* FindTopmostRecursive(
        UIElement& element,
        const math::Vec2& screenPosition,
        const math::Vec2& parentAbsolutePosition,
        const UITransform2D& parentWorldTransform);

    static const UIElement* FindTopmostRecursive(
        const UIElement& element,
        const math::Vec2& screenPosition,
        const math::Vec2& parentAbsolutePosition,
        const UITransform2D& parentWorldTransform);
};

} // namespace Raven
