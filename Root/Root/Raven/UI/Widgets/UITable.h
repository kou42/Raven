#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{

// 列幅は固定ピクセル。行は列数と同じセル数を持つ、Scene等に依存しない表示データです。
struct UITableColumn
{
    std::string Title;
    float Width = 120.0f;
};

class UITable final : public UIElement
{
public:
    using SelectionHandler = std::function<void(std::size_t)>;
    static constexpr std::size_t NoSelection = std::numeric_limits<std::size_t>::max();

    UITable()
    {
        SetFocusable(true);
        SetClipSelf(true);
        SetClipChildren(true);
        SetPreferredSize(math::Vec2(320.0f, 240.0f));
    }

    void SetFont(const Ref<UIFontAtlas>& font) { m_Font = font; }
    void SetOnSelectionChanged(SelectionHandler handler) { m_OnSelectionChanged = std::move(handler); }
    const std::vector<UITableColumn>& GetColumns() const { return m_Columns; }
    const std::vector<std::vector<std::string>>& GetRows() const { return m_Rows; }
    std::size_t GetSelectedIndex() const { return m_SelectedIndex; }

    bool AddColumn(std::string title, float width)
    {
        if (std::isfinite(width) == false || width <= 0.0f)
        {
            return false;
        }
        m_Columns.push_back({ std::move(title), width });
        for (auto& row : m_Rows)
        {
            row.emplace_back();
        }
        InvalidateMeasure();
        return true;
    }

    bool AddRow(std::vector<std::string> cells)
    {
        if (m_Columns.empty() || cells.size() != m_Columns.size())
        {
            return false;
        }
        m_Rows.push_back(std::move(cells));
        InvalidateMeasure();
        return true;
    }

    bool SelectRow(std::size_t index)
    {
        if (index != NoSelection && index >= m_Rows.size())
        {
            return false;
        }
        if (m_SelectedIndex == index)
        {
            return true;
        }
        m_SelectedIndex = index;
        EnsureSelectedVisible();
        if (m_OnSelectionChanged)
        {
            m_OnSelectionChanged(index);
        }
        return true;
    }

    void Clear()
    {
        SelectRow(NoSelection);
        m_Rows.clear();
        m_Columns.clear();
        m_ScrollOffset = 0.0f;
        InvalidateMeasure();
    }

    float GetMaxScrollOffset() const
    {
        return std::max(0.0f, static_cast<float>(m_Rows.size()) * m_RowHeight - BodyHeight());
    }
    float GetScrollOffset() const { return std::min(m_ScrollOffset, GetMaxScrollOffset()); }
    void SetScrollOffset(float value)
    {
        if (std::isfinite(value))
        {
            m_ScrollOffset = std::clamp(value, 0.0f, GetMaxScrollOffset());
        }
    }
    void EnsureSelectedVisible()
    {
        if (m_SelectedIndex >= m_Rows.size() || BodyHeight() <= 0.0f)
        {
            return;
        }
        const float top = static_cast<float>(m_SelectedIndex) * m_RowHeight;
        if (top < GetScrollOffset())
        {
            SetScrollOffset(top);
        }
        else if (top + m_RowHeight > GetScrollOffset() + BodyHeight())
        {
            SetScrollOffset(top + m_RowHeight - BodyHeight());
        }
    }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
        if (event.Type == UIMouseEventType::Scroll)
        {
            const float previous = GetScrollOffset();
            SetScrollOffset(previous - event.ScrollDelta.y * m_WheelScrollStep);
            if (previous != GetScrollOffset())
            {
                event.Handled = true;
            }
            return;
        }
        if (event.Type != UIMouseEventType::Down || event.Button != UIMouseButton::Left ||
            event.Target != this)
        {
            return;
        }
        math::Vec2 local;
        if (TryScreenToLocalPosition(event.ScreenPosition, local) == false ||
            local.x < 0.0f || local.x >= GetSize().x ||
            local.y < m_HeaderHeight || local.y >= GetSize().y)
        {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(
            (local.y - m_HeaderHeight + GetScrollOffset()) / m_RowHeight);
        if (index >= m_Rows.size())
        {
            return;
        }
        if (event.Context != nullptr)
        {
            event.Context->SetFocus(this);
        }
        SelectRow(index);
        event.Handled = true;
    }

    void OnKeyEvent(UIKeyEvent& event) override
    {
        if (IsFocused() == false || event.Pressed == false || event.Control || event.Super ||
            m_Rows.empty())
        {
            return;
        }
        if (event.Key == UIKey::Down)
        {
            SelectRow(m_SelectedIndex == NoSelection ? 0u :
                std::min(m_SelectedIndex + 1u, m_Rows.size() - 1u));
        }
        else if (event.Key == UIKey::Up)
        {
            SelectRow(m_SelectedIndex == NoSelection ? 0u :
                (m_SelectedIndex > 0u ? m_SelectedIndex - 1u : 0u));
        }
        else if (event.Key == UIKey::Home)
        {
            SelectRow(0u);
        }
        else if (event.Key == UIKey::End)
        {
            SelectRow(m_Rows.size() - 1u);
        }
        else
        {
            return;
        }
        event.Handled = true;
    }

    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const override
    {
        const float width = GetSize().x;
        const float height = GetSize().y;
        if (width <= 0.0f || height <= 0.0f)
        {
            return;
        }
        // Headerは固定し、BodyだけScrollします。ClipSelfが行のViewport外描画を切ります。
        float x = position.x;
        const float scroll = GetScrollOffset();
        for (std::size_t row = 0u; row < m_Rows.size(); ++row)
        {
            const float top = m_HeaderHeight + static_cast<float>(row) * m_RowHeight - scroll;
            if (top + m_RowHeight <= m_HeaderHeight)
            {
                continue;
            }
            if (top >= height)
            {
                break;
            }
            const float y = position.y + top;
            if (row == m_SelectedIndex)
            {
                drawList.AddRect(math::Vec2(position.x, std::max(y, position.y + m_HeaderHeight)),
                    math::Vec2(position.x + width, std::min(y + m_RowHeight, position.y + height)),
                    ApplyVisualColor(math::Vec4(0.22f, 0.38f, 0.64f, 1.0f)));
            }
            x = position.x;
            for (std::size_t col = 0u; col < m_Columns.size(); ++col)
            {
                // 行の上下端に跨る文字はClipSelfで制限されます。
                DrawText(drawList, m_Rows[row][col], math::Vec2(x + 5.0f, y + m_Baseline));
                x += m_Columns[col].Width;
            }
        }
        // BodyがHeaderへ重ならないようHeaderを最後に重ねて描画します。
        drawList.AddRect(position, math::Vec2(position.x + width, position.y + m_HeaderHeight),
            ApplyVisualColor(math::Vec4(0.17f, 0.19f, 0.23f, 1.0f)));
        x = position.x;
        for (const auto& column : m_Columns)
        {
            DrawText(drawList, column.Title, math::Vec2(x + 5.0f, position.y + m_Baseline));
            x += column.Width;
        }
    }

private:
    float BodyHeight() const { return std::max(0.0f, GetSize().y - m_HeaderHeight); }
    void DrawText(UIDrawList& drawList, const std::string& text, const math::Vec2& position) const
    {
        if (m_Font == nullptr || m_Font->GetTexture() == nullptr)
        {
            return;
        }
        UITextLayoutOptions options{};
        options.Wrap = UITextWrapMode::None;
        m_Font->AppendText(drawList, text, position, options,
            ApplyVisualColor(math::Vec4(1.0f, 1.0f, 1.0f, 1.0f)));
    }

    std::vector<UITableColumn> m_Columns;
    std::vector<std::vector<std::string>> m_Rows;
    Ref<UIFontAtlas> m_Font;
    SelectionHandler m_OnSelectionChanged;
    std::size_t m_SelectedIndex = NoSelection;
    float m_ScrollOffset = 0.0f;
    float m_HeaderHeight = 28.0f;
    float m_RowHeight = 24.0f;
    float m_Baseline = 18.0f;
    float m_WheelScrollStep = 48.0f;
};

} // namespace Raven
