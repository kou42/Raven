#include "Raven/UI/Core/UIDrawList.h"

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
