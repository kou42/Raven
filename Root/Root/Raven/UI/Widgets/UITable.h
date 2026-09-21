#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Widgets/UIScrollBarMetrics.h"

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
    using RowCountProvider = std::function<std::size_t()>;
    using CellTextProvider = std::function<std::string(std::size_t, std::size_t)>;
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
    std::size_t GetRowCount() const
    {
        return m_RowCountProvider ? m_RowCountProvider() : m_Rows.size();
    }
    bool HasDataSource() const { return static_cast<bool>(m_RowCountProvider); }

    // 外部モデルはTableが所有しません。ProviderはTableより長く生存するデータを参照してください。
    // 内部行との混在を避け、切替時には既存行・選択・Scrollをリセットします。
    bool SetDataSource(RowCountProvider rowCount, CellTextProvider cellText)
    {
        if (rowCount == nullptr || cellText == nullptr)
        {
            return false;
        }
        EndColumnResize();
        EndScrollBarDrag();
        EndHorizontalDrag();
        SelectRow(NoSelection);
        m_Rows.clear();
        m_ScrollOffset = 0.0f;
        m_HorizontalOffset = 0.0f;
        // 列定義は表示設定なので維持し、内部行だけ外部モデルへ切り替えます。
        m_RowCountProvider = std::move(rowCount);
        m_CellTextProvider = std::move(cellText);
        InvalidateMeasure();
        return true;
    }

    void ClearDataSource()
    {
        if (HasDataSource() == false)
        {
            return;
        }
        EndColumnResize();
        EndScrollBarDrag();
        EndHorizontalDrag();
        SelectRow(NoSelection);
        m_RowCountProvider = nullptr;
        m_CellTextProvider = nullptr;
        m_ScrollOffset = 0.0f;
        m_HorizontalOffset = 0.0f;
        InvalidateMeasure();
    }

    // 外部データの件数が変わった際に呼び、無効な選択とScrollを補正します。
    void NotifyDataSourceChanged()
    {
        if (HasDataSource() == false)
        {
            return;
        }
        if (m_SelectedIndex != NoSelection && m_SelectedIndex >= GetRowCount())
        {
            SelectRow(NoSelection);
        }
        SetScrollOffset(m_ScrollOffset);
        InvalidateMeasure();
    }

    // 描画対象の行区間は半開区間[first, last)。行データ全体を走査せずViewportから算出します。
    std::pair<std::size_t, std::size_t> GetVisibleRowRange() const
    {
        const float viewport = BodyHeight();
        if (viewport <= 0.0f || GetRowCount() == 0u)
        {
            return { 0u, 0u };
        }
        const float scroll = GetScrollOffset();
        const std::size_t count = GetRowCount();
        const std::size_t first = std::min(count,
            static_cast<std::size_t>(scroll / m_RowHeight));
        const std::size_t last = std::min(count,
            static_cast<std::size_t>(std::ceil((scroll + viewport) / m_RowHeight)));
        return { first, std::max(first, last) };
    }

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
        if (HasDataSource() == true || m_Columns.empty() || cells.size() != m_Columns.size())
        {
            return false;
        }
        m_Rows.push_back(std::move(cells));
        InvalidateMeasure();
        return true;
    }

    bool SelectRow(std::size_t index)
    {
        if (index != NoSelection && index >= GetRowCount())
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

    bool SetColumnWidth(std::size_t index, float width)
    {
        if (index >= m_Columns.size() || std::isfinite(width) == false || width < m_MinColumnWidth)
        {
            return false;
        }
        m_Columns[index].Width = width;
        InvalidateMeasure();
        return true;
    }

    void Clear()
    {
        EndColumnResize();
        EndScrollBarDrag();
        EndHorizontalDrag();
        SelectRow(NoSelection);
        m_Rows.clear();
        m_Columns.clear();
        m_RowCountProvider = nullptr;
        m_CellTextProvider = nullptr;
        m_ScrollOffset = 0.0f;
        m_HorizontalOffset = 0.0f;
        InvalidateMeasure();
    }

    float GetMaxHorizontalOffset() const
    {
        return std::max(0.0f, TotalColumnWidth() - ContentWidth());
    }
    float GetHorizontalOffset() const
    {
        return std::min(m_HorizontalOffset, GetMaxHorizontalOffset());
    }
    void SetHorizontalOffset(float value)
    {
        if (std::isfinite(value))
        {
            m_HorizontalOffset = std::clamp(value, 0.0f, GetMaxHorizontalOffset());
        }
    }
    bool IsHorizontalScrollBarVisible() const
    {
        // 縦ScrollbarはOverlay表示のため、横方向のOverflow判定にはTable全幅を使います。
        // これにより縦Scrollbarの出現だけで横Scrollbarが連鎖表示されることを防ぎます。
        return TotalColumnWidth() > GetSize().x;
    }
    bool IsScrollBarVisible() const { return GetMaxScrollOffset() > 0.0f && BodyHeight() > 0.0f; }
    float GetMaxScrollOffset() const
    {
        return std::max(0.0f, static_cast<float>(GetRowCount()) * m_RowHeight - BodyHeight());
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
        if (m_SelectedIndex >= GetRowCount() || BodyHeight() <= 0.0f)
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
        if (m_DraggingHorizontal == true)
        {
            if (event.Type == UIMouseEventType::Cancel ||
                (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left))
            {
                EndHorizontalDrag();
                event.Handled = true;
                return;
            }
            if (event.Type == UIMouseEventType::Move)
            {
                math::Vec2 local;
                if (TryScreenToLocalPosition(event.ScreenPosition, local) == true)
                {
                    SetHorizontalOffset(HorizontalMetrics().OffsetFromThumbStart(
                        local.x - m_HorizontalGrabOffset));
                }
                event.Handled = true;
                return;
            }
        }
        if (m_DraggingScrollBar == true)
        {
            if (event.Type == UIMouseEventType::Cancel ||
                (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left))
            {
                EndScrollBarDrag();
                event.Handled = true;
                return;
            }
            if (event.Type == UIMouseEventType::Move)
            {
                math::Vec2 local;
                if (TryScreenToLocalPosition(event.ScreenPosition, local) == true)
                {
                    SetScrollOffset(ScrollMetrics().OffsetFromThumbStart(
                        local.y - m_HeaderHeight - m_ThumbGrabOffset));
                }
                event.Handled = true;
                return;
            }
        }
        if (m_ResizingColumn != NoSelection)
        {
            if (event.Type == UIMouseEventType::Cancel ||
                (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left))
            {
                EndColumnResize();
                event.Handled = true;
                return;
            }
            if (event.Type == UIMouseEventType::Move)
            {
                math::Vec2 local;
                if (TryScreenToLocalPosition(event.ScreenPosition, local) == true)
                {
                    SetColumnWidth(m_ResizingColumn, std::max(m_MinColumnWidth,
                        m_ResizeInitialWidth + local.x - m_ResizeStartX));
                }
                event.Handled = true;
                return;
            }
        }
        if (event.Type == UIMouseEventType::Scroll)
        {
            const float previous = GetScrollOffset();
            const float previousHorizontal = GetHorizontalOffset();
            SetScrollOffset(previous - event.ScrollDelta.y * m_WheelScrollStep);
            SetHorizontalOffset(previousHorizontal - event.ScrollDelta.x * m_WheelScrollStep);
            if (previous != GetScrollOffset() || previousHorizontal != GetHorizontalOffset())
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
            local.y < 0.0f || local.y >= GetSize().y)
        {
            return;
        }
        if (IsHorizontalScrollBarVisible() == true &&
            local.y >= GetSize().y - m_ScrollBarThickness &&
            local.x < ContentWidth())
        {
            const float start = HorizontalMetrics().ThumbStart();
            const float length = HorizontalMetrics().ThumbLength();
            if (local.x >= start && local.x < start + length)
            {
                if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
                {
                    m_DraggingHorizontal = true;
                    m_HorizontalGrabOffset = local.x - start;
                }
            }
            else
            {
                SetHorizontalOffset(GetHorizontalOffset() +
                    (local.x < start ? -ContentWidth() : ContentWidth()));
            }
            event.Handled = true;
            return;
        }
        if (IsScrollBarVisible() == true && local.y >= m_HeaderHeight &&
            local.x >= GetSize().x - m_ScrollBarThickness)
        {
            const float start = ThumbStart();
            if (local.y >= start && local.y < start + ThumbLength())
            {
                if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
                {
                    m_DraggingScrollBar = true;
                    m_ThumbGrabOffset = local.y - start;
                }
            }
            else
            {
                // TrackクリックはBodyの1ページ分移動し、背後の行選択を変更しません。
                SetScrollOffset(GetScrollOffset() +
                    (local.y < start ? -BodyHeight() : BodyHeight()));
            }
            event.Handled = true;
            return;
        }
        if (local.x >= ContentWidth())
        {
            return;
        }
        if (local.y < m_HeaderHeight)
        {
            float edge = 0.0f;
            for (std::size_t column = 0u; column < m_Columns.size(); ++column)
            {
                edge += m_Columns[column].Width;
                if (edge - GetHorizontalOffset() <= ContentWidth() &&
                    std::abs(local.x - (edge - GetHorizontalOffset())) <= m_ResizeHitMargin)
                {
                    if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
                    {
                        m_ResizingColumn = column;
                        m_ResizeStartX = local.x;
                        m_ResizeInitialWidth = m_Columns[column].Width;
                    }
                    event.Handled = true;
                    return;
                }
            }
            return;
        }
        const std::size_t index = static_cast<std::size_t>(
            (local.y - m_HeaderHeight + GetScrollOffset()) / m_RowHeight);
        if (index >= GetRowCount())
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
            GetRowCount() == 0u)
        {
            return;
        }
        if (event.Key == UIKey::Down)
        {
            SelectRow(m_SelectedIndex == NoSelection ? 0u :
                std::min(m_SelectedIndex + 1u, GetRowCount() - 1u));
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
            SelectRow(GetRowCount() - 1u);
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
        const auto visibleRows = GetVisibleRowRange();
        for (std::size_t row = visibleRows.first; row < visibleRows.second; ++row)
        {
            const float top = m_HeaderHeight + static_cast<float>(row) * m_RowHeight - scroll;
            const float y = position.y + top;
            if (row == m_SelectedIndex)
            {
                drawList.AddRect(math::Vec2(position.x, std::max(y, position.y + m_HeaderHeight)),
                    math::Vec2(position.x + ContentWidth(), std::min(y + m_RowHeight, position.y + m_HeaderHeight + BodyHeight())),
                    ApplyVisualColor(math::Vec4(0.22f, 0.38f, 0.64f, 1.0f)));
            }
            x = position.x - GetHorizontalOffset();
            for (std::size_t col = 0u; col < m_Columns.size(); ++col)
            {
                // 横方向も表示範囲外のセルでは文字レイアウト/Command生成を行いません。
                if (x >= position.x + ContentWidth())
                {
                    break;
                }
                if (x + m_Columns[col].Width <= position.x)
                {
                    x += m_Columns[col].Width;
                    continue;
                }
                // 各セルの横境界とBodyの縦境界を交差し、隣の列や固定Headerへの文字漏れを防ぎます。
                UIRect cellClip;
                cellClip.Min = math::Vec2(std::max(x, position.x),
                    std::max(y, position.y + m_HeaderHeight));
                cellClip.Max = math::Vec2(std::min(x + m_Columns[col].Width, position.x + ContentWidth()),
                    std::min(y + m_RowHeight, position.y + m_HeaderHeight + BodyHeight()));
                if (cellClip.Max.x > cellClip.Min.x && cellClip.Max.y > cellClip.Min.y)
                {
                    DrawText(drawList, HasDataSource() == true ? m_CellTextProvider(row, col) : m_Rows[row][col],
                        math::Vec2(x + 5.0f, y + m_Baseline), cellClip);
                }
                x += m_Columns[col].Width;
            }
        }
        if (IsHorizontalScrollBarVisible() == true)
        {
            const float top = position.y + height - m_ScrollBarThickness;
            drawList.AddRect(math::Vec2(position.x, top),
                math::Vec2(position.x + ContentWidth(), position.y + m_HeaderHeight + BodyHeight()),
                ApplyVisualColor(math::Vec4(0.06f, 0.07f, 0.09f, 0.75f)));
            const float left = position.x + HorizontalMetrics().ThumbStart();
            drawList.AddRect(math::Vec2(left, top),
                math::Vec2(left + HorizontalMetrics().ThumbLength(), position.y + height),
                ApplyVisualColor(m_DraggingHorizontal == true
                    ? math::Vec4(0.62f, 0.65f, 0.72f, 0.95f)
                    : math::Vec4(0.42f, 0.45f, 0.52f, 0.95f)));
        }
        if (IsScrollBarVisible() == true)
        {
            const float left = position.x + width - m_ScrollBarThickness;
            drawList.AddRect(math::Vec2(left, position.y + m_HeaderHeight),
                math::Vec2(position.x + width, position.y + height),
                ApplyVisualColor(math::Vec4(0.06f, 0.07f, 0.09f, 0.75f)));
            const float top = position.y + ThumbStart();
            drawList.AddRect(math::Vec2(left, top),
                math::Vec2(position.x + width, top + ThumbLength()),
                ApplyVisualColor(m_DraggingScrollBar == true
                    ? math::Vec4(0.62f, 0.65f, 0.72f, 0.95f)
                    : math::Vec4(0.42f, 0.45f, 0.52f, 0.95f)));
        }
        // BodyがHeaderへ重ならないようHeaderを最後に重ねて描画します。
        drawList.AddRect(position, math::Vec2(position.x + width, position.y + m_HeaderHeight),
            ApplyVisualColor(math::Vec4(0.17f, 0.19f, 0.23f, 1.0f)));
        x = position.x - GetHorizontalOffset();
        for (const auto& column : m_Columns)
        {
            if (x >= position.x + ContentWidth())
            {
                break;
            }
            if (x + column.Width <= position.x)
            {
                x += column.Width;
                continue;
            }
            UIRect headerClip;
            headerClip.Min = math::Vec2(std::max(x, position.x), position.y);
            headerClip.Max = math::Vec2(std::min(x + column.Width, position.x + ContentWidth()),
                position.y + std::min(m_HeaderHeight, height));
            if (headerClip.Max.x > headerClip.Min.x && headerClip.Max.y > headerClip.Min.y)
            {
                DrawText(drawList, column.Title,
                    math::Vec2(x + 5.0f, position.y + m_Baseline), headerClip);
            }
            x += column.Width;
        }
    }

private:
    float TotalColumnWidth() const
    {
        float width = 0.0f;
        for (const auto& column : m_Columns)
        {
            width += column.Width;
        }
        return width;
    }

    float ContentWidth() const
    {
        return std::max(0.0f, GetSize().x -
            (IsScrollBarVisible() == true ? m_ScrollBarThickness : 0.0f));
    }

    UIScrollBarMetrics HorizontalMetrics() const
    {
        return UIScrollBarMetrics{ ContentWidth(), TotalColumnWidth(), GetHorizontalOffset() };
    }

    void EndHorizontalDrag()
    {
        if (m_DraggingHorizontal == false)
        {
            return;
        }
        m_DraggingHorizontal = false;
        UIContext* context = GetContext();
        if (context != nullptr && context->HasMouseCapture(this) == true)
        {
            context->ReleaseMouseCapture(this);
        }
    }

    UIScrollBarMetrics ScrollMetrics() const
    {
        return UIScrollBarMetrics{ BodyHeight(),
            static_cast<float>(m_Rows.size()) * m_RowHeight, GetScrollOffset() };
    }

    float ThumbLength() const { return ScrollMetrics().ThumbLength(); }
    float ThumbStart() const { return m_HeaderHeight + ScrollMetrics().ThumbStart(); }

    void EndScrollBarDrag()
    {
        if (m_DraggingScrollBar == false)
        {
            return;
        }
        m_DraggingScrollBar = false;
        UIContext* context = GetContext();
        if (context != nullptr && context->HasMouseCapture(this) == true)
        {
            context->ReleaseMouseCapture(this);
        }
    }

    void EndColumnResize()
    {
        if (m_ResizingColumn == NoSelection)
        {
            return;
        }
        m_ResizingColumn = NoSelection;
        UIContext* context = GetContext();
        if (context != nullptr && context->HasMouseCapture(this) == true)
        {
            context->ReleaseMouseCapture(this);
        }
    }

    float BodyHeight() const { return std::max(0.0f, GetSize().y - m_HeaderHeight -
        (IsHorizontalScrollBarVisible() == true ? m_ScrollBarThickness : 0.0f)); }
    void DrawText(UIDrawList& drawList, const std::string& text, const math::Vec2& position, const UIRect& clip) const
    {
        if (m_Font == nullptr || m_Font->GetTexture() == nullptr)
        {
            return;
        }
        UITextLayoutOptions options{};
        options.Wrap = UITextWrapMode::None;
        const std::size_t firstCommand = drawList.GetCommandCount();
        m_Font->AppendText(drawList, text, position, options,
            ApplyVisualColor(math::Vec4(1.0f, 1.0f, 1.0f, 1.0f)));
        // DrawListのElement Clip適用時にも、このセル固有のClipを交差して保持します。
        drawList.ApplyClip(firstCommand, UIClipRect::FromRect(clip));
    }

    RowCountProvider m_RowCountProvider;
    CellTextProvider m_CellTextProvider;
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
    float m_MinColumnWidth = 32.0f;
    float m_ResizeHitMargin = 5.0f;
    float m_ResizeStartX = 0.0f;
    float m_ResizeInitialWidth = 0.0f;
    std::size_t m_ResizingColumn = NoSelection;
    float m_ScrollBarThickness = 10.0f;
    float m_ThumbGrabOffset = 0.0f;
    bool m_DraggingScrollBar = false;
    float m_HorizontalOffset = 0.0f;
    float m_HorizontalGrabOffset = 0.0f;
    bool m_DraggingHorizontal = false;
};

} // namespace Raven
