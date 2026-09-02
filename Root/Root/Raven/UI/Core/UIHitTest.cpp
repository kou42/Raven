#include "Raven/UI/Core/UIHitTest.h"
#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

UIElement* UIHitTest::FindTopmost(UIElement& root, const math::Vec2& screenPosition)
{
    return FindTopmostRecursive(
        root,
        screenPosition,
        math::Vec2(0.0f, 0.0f),
        UITransform2D::Identity());
}

const UIElement* UIHitTest::FindTopmost(const UIElement& root, const math::Vec2& screenPosition)
{
    return FindTopmostRecursive(
        root,
        screenPosition,
        math::Vec2(0.0f, 0.0f),
        UITransform2D::Identity());
}

bool UIHitTest::ContainsPoint(
    const UIElement& element,
    const math::Vec2& absolutePosition,
    const UITransform2D& worldTransform,
    const math::Vec2& screenPosition)
{
    math::Vec2 layoutPoint;

    // Rendererと同じ合成済みWorld Transformを逆変換し、Screen座標をTransform適用前の
    // Layout座標へ戻してから矩形判定します。親の非一様Scale + 子RotationでShearが生じても、
    // Affine行列全体を逆変換するため描画領域とHit領域を一致させられます。
    if (worldTransform.TryInverseTransformPoint(screenPosition, layoutPoint) == false)
    {
        return false;
    }

    const math::Vec2& size = element.GetSize();

    // 左上を含み右下を含まない半開区間に統一します。
    // 隣接するElementの境界上で2つが同時にHitする曖昧さを避けるためです。
    return layoutPoint.x >= absolutePosition.x &&
        layoutPoint.y >= absolutePosition.y &&
        layoutPoint.x < absolutePosition.x + size.x &&
        layoutPoint.y < absolutePosition.y + size.y;
}

UIElement* UIHitTest::FindTopmostRecursive(
    UIElement& element,
    const math::Vec2& screenPosition,
    const math::Vec2& parentAbsolutePosition,
    const UITransform2D& parentWorldTransform)
{
    if (element.IsVisible() == false)
    {
        return nullptr;
    }

    const math::Vec2& localPosition = element.GetPosition();
    const math::Vec2 absolutePosition(
        parentAbsolutePosition.x + localPosition.x,
        parentAbsolutePosition.y + localPosition.y);

    const math::Vec2& size = element.GetSize();
    const math::Vec2& pivotNormalized = element.GetTransformPivot();
    const math::Vec2 pivot(
        absolutePosition.x + size.x * pivotNormalized.x,
        absolutePosition.y + size.y * pivotNormalized.y);
    const UITransform2D localTransform = UITransform2D::CreateScaleRotation(
        pivot,
        element.GetRotation(),
        element.GetScale());
    const UITransform2D worldTransform = UITransform2D::Combine(
        parentWorldTransform,
        localTransform);

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
            absolutePosition,
            worldTransform);

        if (hit != nullptr)
        {
            return hit;
        }
    }

    if (ContainsPoint(element, absolutePosition, worldTransform, screenPosition) == true)
    {
        return &element;
    }

    return nullptr;
}

const UIElement* UIHitTest::FindTopmostRecursive(
    const UIElement& element,
    const math::Vec2& screenPosition,
    const math::Vec2& parentAbsolutePosition,
    const UITransform2D& parentWorldTransform)
{
    if (element.IsVisible() == false)
    {
        return nullptr;
    }

    const math::Vec2& localPosition = element.GetPosition();
    const math::Vec2 absolutePosition(
        parentAbsolutePosition.x + localPosition.x,
        parentAbsolutePosition.y + localPosition.y);

    const math::Vec2& size = element.GetSize();
    const math::Vec2& pivotNormalized = element.GetTransformPivot();
    const math::Vec2 pivot(
        absolutePosition.x + size.x * pivotNormalized.x,
        absolutePosition.y + size.y * pivotNormalized.y);
    const UITransform2D localTransform = UITransform2D::CreateScaleRotation(
        pivot,
        element.GetRotation(),
        element.GetScale());
    const UITransform2D worldTransform = UITransform2D::Combine(
        parentWorldTransform,
        localTransform);

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
            absolutePosition,
            worldTransform);

        if (hit != nullptr)
        {
            return hit;
        }
    }

    if (ContainsPoint(element, absolutePosition, worldTransform, screenPosition) == true)
    {
        return &element;
    }

    return nullptr;
}

} // namespace Raven
