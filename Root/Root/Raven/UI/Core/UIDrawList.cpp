#include "Raven/UI/Core/UIDrawList.h"

#include "Raven/Assets/TextureAsset.h"

#include <algorithm>
#include <cmath>

namespace Raven
{

UIClipRect UIClipRect::Disabled()
{
    return UIClipRect{};
}

UIClipRect UIClipRect::FromRect(const UIRect& rect)
{
    UIClipRect result;
    result.Rect = rect;
    result.Enabled = true;
    return result;
}

UIClipRect UIClipRect::Intersect(const UIClipRect& inheritedClip, const UIRect& rect)
{
    if (inheritedClip.Enabled == false)
    {
        return FromRect(rect);
    }

    UIClipRect result;
    result.Enabled = true;
    result.Rect.Min.x = std::max(inheritedClip.Rect.Min.x, rect.Min.x);
    result.Rect.Min.y = std::max(inheritedClip.Rect.Min.y, rect.Min.y);
    result.Rect.Max.x = std::min(inheritedClip.Rect.Max.x, rect.Max.x);
    result.Rect.Max.y = std::min(inheritedClip.Rect.Max.y, rect.Max.y);

    // Clipが交差しない場合もEnabled状態を維持し、面積0のScissorとして表現します。
    // これによりDescendant側で「Clipなし」と「完全にClipされた状態」を区別できます。
    if (result.Rect.Max.x < result.Rect.Min.x)
    {
        result.Rect.Max.x = result.Rect.Min.x;
    }
    if (result.Rect.Max.y < result.Rect.Min.y)
    {
        result.Rect.Max.y = result.Rect.Min.y;
    }
    return result;
}

bool UIClipRect::Contains(const math::Vec2& point) const
{
    if (Enabled == false)
    {
        return true;
    }

    return point.x >= Rect.Min.x &&
        point.y >= Rect.Min.y &&
        point.x < Rect.Max.x &&
        point.y < Rect.Max.y;
}

UITransform2D UITransform2D::Identity()
{
    return UITransform2D{};
}

UITransform2D UITransform2D::CreateScaleRotation(
    const math::Vec2& pivot,
    float rotation,
    const math::Vec2& scale)
{
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);

    UITransform2D result;
    result.M00 = cosine * scale.x;
    result.M01 = -sine * scale.y;
    result.M10 = sine * scale.x;
    result.M11 = cosine * scale.y;

    // pivotを不変点にするため、T(pivot) * R * S * T(-pivot) のTranslationを展開します。
    result.Translation.x = pivot.x - (result.M00 * pivot.x + result.M01 * pivot.y);
    result.Translation.y = pivot.y - (result.M10 * pivot.x + result.M11 * pivot.y);
    return result;
}

UITransform2D UITransform2D::Combine(
    const UITransform2D& parent,
    const UITransform2D& local)
{
    UITransform2D result;

    // parent(local(point)) を直接展開します。
    // Rotation / 非一様Scaleの組み合わせでShearが発生しても2x2線形部へそのまま保持できます。
    result.M00 = parent.M00 * local.M00 + parent.M01 * local.M10;
    result.M01 = parent.M00 * local.M01 + parent.M01 * local.M11;
    result.M10 = parent.M10 * local.M00 + parent.M11 * local.M10;
    result.M11 = parent.M10 * local.M01 + parent.M11 * local.M11;

    result.Translation.x =
        parent.M00 * local.Translation.x +
        parent.M01 * local.Translation.y +
        parent.Translation.x;
    result.Translation.y =
        parent.M10 * local.Translation.x +
        parent.M11 * local.Translation.y +
        parent.Translation.y;
    return result;
}

math::Vec2 UITransform2D::TransformPoint(const math::Vec2& point) const
{
    return math::Vec2(
        M00 * point.x + M01 * point.y + Translation.x,
        M10 * point.x + M11 * point.y + Translation.y);
}

UIRect UITransform2D::TransformRectBounds(const UIRect& rect) const
{
    const math::Vec2 p0 = TransformPoint(rect.Min);
    const math::Vec2 p1 = TransformPoint(math::Vec2(rect.Max.x, rect.Min.y));
    const math::Vec2 p2 = TransformPoint(rect.Max);
    const math::Vec2 p3 = TransformPoint(math::Vec2(rect.Min.x, rect.Max.y));

    UIRect result;
    result.Min.x = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
    result.Min.y = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
    result.Max.x = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    result.Max.y = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
    return result;
}

bool UITransform2D::TryInverseTransformPoint(
    const math::Vec2& point,
    math::Vec2& outPoint) const
{
    const float determinant = M00 * M11 - M01 * M10;
    if (determinant == 0.0f)
    {
        return false;
    }

    const float translatedX = point.x - Translation.x;
    const float translatedY = point.y - Translation.y;
    const float inverseDeterminant = 1.0f / determinant;

    outPoint.x = (M11 * translatedX - M01 * translatedY) * inverseDeterminant;
    outPoint.y = (-M10 * translatedX + M00 * translatedY) * inverseDeterminant;
    return true;
}

void UIDrawList::Clear()
{
    m_Commands.clear();
}

void UIDrawList::AddRect(
    const math::Vec2& min,
    const math::Vec2& max,
    const math::Vec4& color)
{
    // 面積0以下の矩形は描画対象にしません。
    // Layout計算中の非表示Elementや、Window最小化時の0 sizeがそのままRendererへ
    // 流れ込むことを避けます。
    if (max.x <= min.x || max.y <= min.y)
    {
        return;
    }

    UIDrawCommand command;
    command.Type = UIDrawCommandType::SolidRect;
    command.Rect.Min = min;
    command.Rect.Max = max;
    command.Color = color;
    m_Commands.push_back(command);
}

void UIDrawList::AddImage(
    const math::Vec2& min,
    const math::Vec2& max,
    const Ref<TextureAsset>& texture,
    const math::Vec4& tintColor,
    const math::Vec2& uvMin,
    const math::Vec2& uvMax)
{
    // SolidRectと同様、面積0以下のImage CommandはRendererへ流しません。
    // Layout中の0 sizeやWindow最小化時にも不要なDraw Callを生成しないためです。
    if (max.x <= min.x || max.y <= min.y)
    {
        return;
    }

    // WidgetからRenderer IDを直接渡さず、Runtime Assetの有効性だけをUI Coreで確認します。
    // GPU Resourceの解決とBindはUIRenderer backendの責務です。
    if (texture == nullptr || texture->IsValid() == false)
    {
        return;
    }

    UIDrawCommand command;
    command.Type = UIDrawCommandType::Image;
    command.Rect.Min = min;
    command.Rect.Max = max;
    command.Color = tintColor;
    command.UVMin = uvMin;
    command.UVMax = uvMax;
    command.Texture = texture;
    m_Commands.push_back(command);
}

void UIDrawList::ApplyTransform(std::size_t firstCommand, const UITransform2D& transform)
{
    if (firstCommand >= m_Commands.size())
    {
        return;
    }

    // Element単位のWorld Transformは、そのElementが生成したCommandだけへ付与します。
    // ChildはUIElement再帰側でParent World Transformと自身のLocal Transformを合成してから別途設定します。
    for (std::size_t index = firstCommand; index < m_Commands.size(); ++index)
    {
        m_Commands[index].Transform = transform;
    }
}

void UIDrawList::ApplyClip(std::size_t firstCommand, const UIClipRect& clip)
{
    if (firstCommand >= m_Commands.size())
    {
        return;
    }

    for (std::size_t index = firstCommand; index < m_Commands.size(); ++index)
    {
        m_Commands[index].Clip = clip;
    }
}

const std::vector<UIDrawCommand>& UIDrawList::GetCommands() const
{
    return m_Commands;
}

std::size_t UIDrawList::GetCommandCount() const
{
    return m_Commands.size();
}

bool UIDrawList::IsEmpty() const
{
    return m_Commands.empty();
}

} // namespace Raven
