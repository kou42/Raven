#include "Raven/UI/Core/UIDrawList.h"

#include "Raven/Assets/TextureAsset.h"

namespace Raven
{

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
