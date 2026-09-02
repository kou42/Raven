#include "Raven/UI/Core/UIHitTest.h"
#include "Raven/UI/Core/UIElement.h"

#include <cmath>

namespace Raven
{

UIElement* UIHitTest::FindTopmost(UIElement& root, const math::Vec2& screenPosition)
{
    return FindTopmostRecursive(root, screenPosition, math::Vec2(0.0f, 0.0f));
}

const UIElement* UIHitTest::FindTopmost(const UIElement& root, const math::Vec2& screenPosition)
{
    return FindTopmostRecursive(root, screenPosition, math::Vec2(0.0f, 0.0f));
}

bool UIHitTest::ContainsPoint(
    const UIElement& element,
    const math::Vec2& absolutePosition,
    const math::Vec2& screenPosition)
{
    const math::Vec2& size = element.GetSize();
    const math::Vec2& pivotNormalized = element.GetTransformPivot();
    const math::Vec2& scale = element.GetScale();

    // 描画側はPivot中心に Scale -> Rotation の順で変換しているため、Hit Testでは
    // Screen座標へ逆Rotation -> 逆Scaleを適用し、Layout済みのaxis-aligned Rectへ戻して判定します。
    // 描画と入力で同じTransform規約を使うことで、回転・拡縮後も見えている領域とHit領域を一致させます。
    const math::Vec2 pivot(
        absolutePosition.x + size.x * pivotNormalized.x,
        absolutePosition.y + size.y * pivotNormalized.y);

    const float deltaX = screenPosition.x - pivot.x;
    const float deltaY = screenPosition.y - pivot.y;
    const float cosine = std::cos(element.GetRotation());
    const float sine = std::sin(element.GetRotation());

    // forward rotation:
    // x' = x*cos - y*sin
    // y' = x*sin + y*cos
    // inverseは転置行列なので角度を反転した式になります。
    const float rotatedX = deltaX * cosine + deltaY * sine;
    const float rotatedY = -deltaX * sine + deltaY * cosine;

    // Scale 0の軸は描画上も面積0へ潰れているためHit対象にしません。
    // 極端に小さい値をepsilonで近似すると描画との境界がずれるため、ここでは厳密な0だけを特別扱いします。
    if (scale.x == 0.0f || scale.y == 0.0f)
    {
        return false;
    }

    const math::Vec2 localPoint(
        pivot.x + rotatedX / scale.x,
        pivot.y + rotatedY / scale.y);

    // 左上を含み右下を含まない半開区間に統一します。
    // 隣接するElementの境界上で2つが同時にHitする曖昧さを避けるためです。
    return localPoint.x >= absolutePosition.x &&
        localPoint.y >= absolutePosition.y &&
        localPoint.x < absolutePosition.x + size.x &&
        localPoint.y < absolutePosition.y + size.y;
}

UIElement* UIHitTest::FindTopmostRecursive(
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

    if (ContainsPoint(element, absolutePosition, screenPosition) == true)
    {
        return &element;
    }

    return nullptr;
}

const UIElement* UIHitTest::FindTopmostRecursive(
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

    if (ContainsPoint(element, absolutePosition, screenPosition) == true)
    {
        return &element;
    }

    return nullptr;
}

} // namespace Raven
