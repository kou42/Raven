#include "Raven/UI/Widgets/UITabBar.h"

#include "Raven/UI/Core/UIContext.h"

#include "Raven/UI/Text/UITextLayout.h"

#include <algorithm>
#include <cmath>

namespace Raven
{

UITabBar::UITabBar(UITabModel& model)
    : m_Model(model)
{
    SetFocusable(true);
    SetClipSelf(true);
    SetClipChildren(true);
    SetPreferredSize(math::Vec2(300.0f, m_TabHeight));
}

void UITabBar::SetFont(const Ref<UIFontAtlas>& font)
{
    m_Font = font;
    InvalidateMeasure();
}

void UITabBar::SetTabWidth(float width)
{
    if (std::isfinite(width) == true && width > m_CloseWidth + 16.0f)
    {
        m_TabWidth = width;
        InvalidateMeasure();
    }
}

void UITabBar::SetTabHeight(float height)
{
    if (std::isfinite(height) == true && height > 0.0f)
    {
        m_TabHeight = height;
        SetPreferredSize(math::Vec2(GetPreferredSize().x, height));
        InvalidateMeasure();
    }
}

math::Vec2 UITabBar::OnMeasureContent() const
{
    return math::Vec2(static_cast<float>(m_Model.GetTabCount()) * m_TabWidth, m_TabHeight);
}

std::uint64_t UITabBar::HitTab(float x, float y, bool& close) const
{
    close = false;
    if (x < 0.0f || y < 0.0f || x >= GetSize().x ||
        y >= std::min(GetSize().y, m_TabHeight))
    {
        return 0u;
    }
    const std::size_t index = static_cast<std::size_t>(x / m_TabWidth);
    const auto& tabs = m_Model.GetTabs();
    if (index >= tabs.size())
    {
        return 0u;
    }
    const float tabRight = static_cast<float>(index + 1u) * m_TabWidth;
    close = tabs[index].Closable == true && x >= tabRight - m_CloseWidth;
    return tabs[index].Id;
}

void UITabBar::OnMouseEvent(UIMouseEvent& event)
{
    if (event.Type == UIMouseEventType::Cancel)
    {
        m_HoveredId = 0u;
        m_HoveredClose = false;
        m_PendingDragId = 0u;
        m_DropIndicatorVisible = false;
        return;
    }
    if (event.Target != this)
    {
        return;
    }
    math::Vec2 local;
    if (TryScreenToLocalPosition(event.ScreenPosition, local) == false)
    {
        return;
    }
    bool close = false;
    const std::uint64_t id = HitTab(local.x, local.y, close);
    if (event.Type == UIMouseEventType::Move)
    {
        m_HoveredId = id;
        m_HoveredClose = close;
        return;
    }
    if (event.Button != UIMouseButton::Left)
    {
        return;
    }
    if (event.Type == UIMouseEventType::Down && id != 0u)
    {
        if (event.Context != nullptr)
        {
            event.Context->SetFocus(this);
        }
        // Close領域はDrag開始対象にしません。閾値未満のUpは通常Clickへ戻ります。
        if (close == false && m_DragReorderEnabled == true && event.Context != nullptr)
        {
            const UITabItem* tab = m_Model.FindTab(id);
            if (tab != nullptr && event.Context->BeginDrag(this,
                UIDragDropPayload{ "Raven/UITab", std::to_string(id) },
                event.ScreenPosition) == true)
            {
                m_PendingDragId = id;
                event.Context->SetDragPreview(tab->Title, m_Font);
            }
        }
        event.Handled = true;
        return;
    }
    if (event.Type == UIMouseEventType::Up && event.PressedTarget == this && id != 0u)
    {
        // Callbackがモデルを変更し得るため、Tab参照ではなくIDのみを保持します。
        if (close == true)
        {
            m_Model.CloseTab(id);
        }
        else
        {
            m_Model.SelectTab(id);
        }
        m_PendingDragId = 0u;
        event.Handled = true;
    }
}

bool UITabBar::OnDragDropEvent(UIDragDropEvent& event)
{
    if (event.Type == UIDragDropEventType::Leave)
    {
        // 一時的にBar外へ出てもSourceのIDは維持し、戻ったときのDropを許可します。
        m_DropIndicatorVisible = false;
        return false;
    }
    if (event.Type == UIDragDropEventType::Cancel ||
        event.Type == UIDragDropEventType::End)
    {
        m_DropIndicatorVisible = false;
        m_PendingDragId = 0u;
        return false;
    }
    if (m_DragReorderEnabled == false || event.Source != this ||
        event.Payload == nullptr || event.Payload->Type != "Raven/UITab" ||
        m_PendingDragId == 0u ||
        event.Payload->Data != std::to_string(m_PendingDragId))
    {
        return false;
    }
    const auto& tabs = m_Model.GetTabs();
    if (tabs.size() < 2u || m_Model.FindTab(m_PendingDragId) == nullptr)
    {
        return false;
    }
    math::Vec2 local;
    if (TryScreenToLocalPosition(event.ScreenPosition, local) == false ||
        local.x < 0.0f || local.x >= GetSize().x ||
        local.y < 0.0f || local.y >= std::min(GetSize().y, m_TabHeight))
    {
        return false;
    }
    // 境界の左右半分を挿入位置へ変換します。最後尾へのDropも許容します。
    const float slot = local.x / m_TabWidth;
    const std::size_t boundary = std::min(tabs.size(),
        static_cast<std::size_t>(slot + 0.5f));
    const auto source = std::find_if(tabs.begin(), tabs.end(),
        [this](const UITabItem& tab) { return tab.Id == m_PendingDragId; });
    const std::size_t from = static_cast<std::size_t>(source - tabs.begin());
    const std::size_t to = boundary > from ? boundary - 1u : boundary;
    if (to == from)
    {
        m_DropIndicatorVisible = false;
        return false;
    }
    if (event.Type == UIDragDropEventType::Over)
    {
        m_DropIndex = boundary;
        m_DropIndicatorVisible = true;
        event.Accepted = true;
        return true;
    }
    if (event.Type == UIDragDropEventType::Drop)
    {
        m_DropIndicatorVisible = false;
        return m_Model.MoveTab(m_PendingDragId, to);
    }
    return false;
}

void UITabBar::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const
{
    const float height = std::min(GetSize().y, m_TabHeight);
    drawList.AddRect(position,
        math::Vec2(position.x + GetSize().x, position.y + height),
        ApplyVisualColor(math::Vec4(0.13f, 0.14f, 0.17f, 1.0f)));

    const auto& tabs = m_Model.GetTabs();
    for (std::size_t index = 0u; index < tabs.size(); ++index)
    {
        const float left = static_cast<float>(index) * m_TabWidth;
        if (left >= GetSize().x)
        {
            break;
        }
        const UITabItem& tab = tabs[index];
        const bool selected = tab.Id == m_Model.GetSelectedTabId();
        const bool hovered = tab.Id == m_HoveredId;
        const math::Vec4 color = selected
            ? math::Vec4(0.29f, 0.34f, 0.44f, 1.0f)
            : hovered ? math::Vec4(0.23f, 0.25f, 0.30f, 1.0f)
                : math::Vec4(0.18f, 0.19f, 0.23f, 1.0f);
        drawList.AddRect(math::Vec2(position.x + left, position.y),
            math::Vec2(position.x + left + m_TabWidth - 1.0f, position.y + height),
            ApplyVisualColor(color));
        if (selected == true)
        {
            drawList.AddRect(math::Vec2(position.x + left, position.y + height - 2.0f),
                math::Vec2(position.x + left + m_TabWidth - 1.0f, position.y + height),
                ApplyVisualColor(math::Vec4(0.48f, 0.68f, 0.96f, 1.0f)));
        }
        if (m_Font != nullptr && m_Font->GetTexture() != nullptr)
        {
            UITextLayoutOptions options{};
            options.LineHeight = 20.0f;
            // Textの切り詰め/ellipsisは後続のOverflow対応で追加します。
            options.MaxWidth = m_TabWidth - (tab.Closable == true ? m_CloseWidth : 0.0f) - 16.0f;
            m_Font->AppendText(drawList, tab.Title,
                math::Vec2(position.x + left + 8.0f, position.y + height * 0.5f + 5.0f),
                options, ApplyVisualColor(math::Vec4(0.95f, 0.95f, 0.97f, 1.0f)));
            if (tab.Closable == true)
            {
                const math::Vec4 closeColor = hovered == true && m_HoveredClose == true
                    ? math::Vec4(1.0f, 0.68f, 0.68f, 1.0f)
                    : math::Vec4(0.75f, 0.77f, 0.82f, 1.0f);
                m_Font->AppendText(drawList, "x",
                    math::Vec2(position.x + left + m_TabWidth - m_CloseWidth + 8.0f,
                        position.y + height * 0.5f + 5.0f),
                    options, ApplyVisualColor(closeColor));
            }
        }
    }
    if (m_DropIndicatorVisible == true)
    {
        const float x = position.x + static_cast<float>(m_DropIndex) * m_TabWidth;
        drawList.AddRect(math::Vec2(x - 2.0f, position.y + 3.0f),
            math::Vec2(x + 2.0f, position.y + height - 3.0f),
            ApplyVisualColor(math::Vec4(0.55f, 0.78f, 1.0f, 1.0f)));
    }
}

} // namespace Raven
