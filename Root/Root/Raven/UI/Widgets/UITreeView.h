#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Widgets/UIScrollBarMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{

// Scene/Entityへの依存を持たないTreeViewの表示ノードです。
// 所有権はTreeView/親Nodeが持ち、追加後のNodeアドレスは兄弟の追加で変化しません。
// 削除/Clear後はNode*が無効になるため、外部で長期保持する場合はIDを利用してください。
struct UITreeNode
{
    std::uint64_t Id = 0u;
    std::string Text;
    UITreeNode* Parent = nullptr;
    bool Expanded = true;
    std::vector<std::unique_ptr<UITreeNode>> Children;
};

// 最小のRetained TreeView。描画・Hit判定は展開済みNodeの同一の深さ優先順序を使います。
// Wheel縦Scroll・Scrollbar・選択行の可視化に対応します。仮想化/複数選択は別段階です。
class UITreeView final : public UIElement
{
public:
    using SelectionHandler = std::function<void(std::uint64_t)>;
    using ExpansionHandler = std::function<void(std::uint64_t, bool)>;

    UITreeView()
    {
        SetFocusable(true);
        // UIElementはClipChildrenでViewport矩形を構築し、ClipSelfで自身の描画にも適用します。
        SetClipChildren(true);
        SetClipSelf(true);
        SetPreferredSize(math::Vec2(240.0f, 240.0f));
    }

    UITreeNode* AddRoot(std::uint64_t id, std::string text)
    {
        return AddNode(nullptr, id, std::move(text));
    }

    UITreeNode* AddNode(UITreeNode* parent, std::uint64_t id, std::string text)
    {
        // IDの重複と別Tree所属のParentを拒否し、選択の識別を安定させます。
        if (FindNode(id) != nullptr || (parent != nullptr && Contains(parent) == false))
        {
            return nullptr;
        }
        auto node = std::make_unique<UITreeNode>();
        node->Id = id;
        node->Text = std::move(text);
        node->Parent = parent;
        UITreeNode* result = node.get();
        if (parent != nullptr)
        {
            parent->Children.push_back(std::move(node));
        }
        else
        {
            m_Roots.push_back(std::move(node));
        }
        InvalidateMeasure();
        return result;
    }

    void Clear()
    {
        EndScrollBarDrag();
        // Callbackへ渡したNode*も無効になるので、先に選択を解除します。
        Select(nullptr);
        m_Roots.clear();
        m_ScrollOffset = 0.0f;
        InvalidateMeasure();
    }

    UITreeNode* FindNode(std::uint64_t id) const
    {
        std::vector<UITreeNode*> pending;
        for (const auto& root : m_Roots)
        {
            pending.push_back(root.get());
        }
        while (pending.empty() == false)
        {
            UITreeNode* node = pending.back();
            pending.pop_back();
            if (node->Id == id)
            {
                return node;
            }
            for (const auto& child : node->Children)
            {
                pending.push_back(child.get());
            }
        }
        return nullptr;
    }

    bool Select(UITreeNode* node)
    {
        if (node != nullptr && Contains(node) == false)
        {
            return false;
        }
        if (m_Selected == node)
        {
            return true;
        }
        m_Selected = node;
        EnsureSelectedVisible();
        if (m_OnSelectionChanged)
        {
            m_OnSelectionChanged(node != nullptr ? node->Id : 0u);
        }
        return true;
    }

    UITreeNode* GetSelectedNode() const { return m_Selected; }

    bool IsScrollBarVisible() const { return GetMaxScrollOffset() > 0.0f; }
    void SetScrollBarThickness(float value)
    {
        if (std::isfinite(value) && value > 0.0f)
        {
            m_ScrollBarThickness = value;
        }
    }
    float GetScrollBarThickness() const { return m_ScrollBarThickness; }

    float GetScrollOffset() const { return std::min(m_ScrollOffset, GetMaxScrollOffset()); }
    float GetMaxScrollOffset() const
    {
        return std::max(0.0f, static_cast<float>(VisibleNodes().size()) * m_RowHeight - GetSize().y);
    }
    void SetScrollOffset(float value)
    {
        if (std::isfinite(value))
        {
            m_ScrollOffset = std::clamp(value, 0.0f, GetMaxScrollOffset());
        }
    }
    void SetWheelScrollStep(float value)
    {
        if (std::isfinite(value) && value > 0.0f)
        {
            m_WheelScrollStep = value;
        }
    }
    void EnsureSelectedVisible()
    {
        if (m_Selected == nullptr || GetSize().y <= 0.0f)
        {
            return;
        }
        const auto visible = VisibleNodes();
        const auto it = std::find_if(visible.begin(), visible.end(),
            [this](const auto& entry) { return entry.first == m_Selected; });
        if (it == visible.end())
        {
            return;
        }
        const float top = static_cast<float>(it - visible.begin()) * m_RowHeight;
        if (top < GetScrollOffset())
        {
            SetScrollOffset(top);
        }
        else if (top + m_RowHeight > GetScrollOffset() + GetSize().y)
        {
            SetScrollOffset(top + m_RowHeight - GetSize().y);
        }
    }
    void SetOnSelectionChanged(SelectionHandler handler) { m_OnSelectionChanged = std::move(handler); }
    void SetOnExpansionChanged(ExpansionHandler handler) { m_OnExpansionChanged = std::move(handler); }
    void SetFont(const Ref<UIFontAtlas>& font) { m_Font = font; }
    void SetRowHeight(float value)
    {
        if (std::isfinite(value) && value > 0.0f)
        {
            m_RowHeight = value;
            InvalidateMeasure();
            SetScrollOffset(m_ScrollOffset);
            EnsureSelectedVisible();
        }
    }

    bool SetExpanded(UITreeNode* node, bool expanded)
    {
        if (node == nullptr || Contains(node) == false || node->Children.empty())
        {
            return false;
        }
        if (node->Expanded == expanded)
        {
            return true;
        }
        // 折りたたみで選択行が隠れる場合は、可視の親へ選択を移します。
        if (expanded == false && m_Selected != nullptr && m_Selected != node)
        {
            for (UITreeNode* ancestor = m_Selected->Parent; ancestor != nullptr; ancestor = ancestor->Parent)
            {
                if (ancestor == node)
                {
                    Select(node);
                    break;
                }
            }
        }
        node->Expanded = expanded;
        InvalidateMeasure();
        SetScrollOffset(m_ScrollOffset);
        if (m_OnExpansionChanged)
        {
            m_OnExpansionChanged(node->Id, expanded);
        }
        return true;
    }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
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
                    SetScrollOffset(ScrollMetrics().OffsetFromThumbStart(local.y - m_DragGrabOffset));
                }
                event.Handled = true;
                return;
            }
        }
        if (event.Type == UIMouseEventType::Scroll)
        {
            const float previous = GetScrollOffset();
            SetScrollOffset(previous - event.ScrollDelta.y * m_WheelScrollStep);
            // 境界では消費せず、親ScrollViewへWheelを伝播させます。
            if (GetScrollOffset() != previous)
            {
                event.Handled = true;
            }
            return;
        }
        if (event.Type != UIMouseEventType::Down || event.Button != UIMouseButton::Left || event.Target != this)
        {
            return;
        }
        math::Vec2 local;
        if (TryScreenToLocalPosition(event.ScreenPosition, local) == false ||
            local.x < 0.0f || local.y < 0.0f || local.x >= GetSize().x || local.y >= GetSize().y)
        {
            return;
        }
        // ScrollbarのHit判定を行より優先し、Trackクリックは1ページ分移動します。
        if (IsScrollBarVisible() == true &&
            local.x >= GetSize().x - m_ScrollBarThickness)
        {
            const float thumbStart = GetThumbStart();
            const float thumbLength = GetThumbLength();
            if (local.y >= thumbStart && local.y < thumbStart + thumbLength)
            {
                if (event.Context != nullptr && event.Context->CaptureMouse(this) == true)
                {
                    m_DraggingScrollBar = true;
                    m_DragGrabOffset = local.y - thumbStart;
                }
            }
            else
            {
                SetScrollOffset(GetScrollOffset() +
                    (local.y < thumbStart ? -GetSize().y : GetSize().y));
            }
            event.Handled = true;
            return;
        }
        const auto visible = VisibleNodes();
        const std::size_t index = static_cast<std::size_t>((local.y + GetScrollOffset()) / m_RowHeight);
        if (index >= visible.size())
        {
            return;
        }
        UITreeNode* node = visible[index].first;
        const float indent = static_cast<float>(visible[index].second) * m_Indent;
        if (event.Context != nullptr)
        {
            event.Context->SetFocus(this);
        }
        // Disclosure領域のクリックは選択を変更せず、展開状態だけを切り替えます。
        if (local.x >= indent && local.x < indent + m_Indent && node->Children.empty() == false)
        {
            SetExpanded(node, node->Expanded == false);
        }
        else
        {
            Select(node);
        }
        event.Handled = true;
    }

    void OnKeyEvent(UIKeyEvent& event) override
    {
        if (IsFocused() == false || event.Pressed == false || event.Control || event.Super)
        {
            return;
        }
        const auto visible = VisibleNodes();
        if (visible.empty())
        {
            return;
        }
        auto it = std::find_if(visible.begin(), visible.end(),
            [this](const auto& entry) { return entry.first == m_Selected; });
        const std::size_t index = it == visible.end() ? visible.size() : static_cast<std::size_t>(it - visible.begin());
        if (event.Key == UIKey::Down)
        {
            Select(visible[index == visible.size() ? 0u : std::min(index + 1u, visible.size() - 1u)].first);
        }
        else if (event.Key == UIKey::Up)
        {
            Select(visible[index == visible.size() ? 0u : (index > 0u ? index - 1u : 0u)].first);
        }
        else if (event.Key == UIKey::Right && index < visible.size())
        {
            UITreeNode* node = visible[index].first;
            if (node->Children.empty() == false)
            {
                if (node->Expanded == false) { SetExpanded(node, true); }
                else { Select(node->Children.front().get()); }
            }
        }
        else if (event.Key == UIKey::Left && index < visible.size())
        {
            UITreeNode* node = visible[index].first;
            if (node->Expanded && node->Children.empty() == false) { SetExpanded(node, false); }
            else if (node->Parent != nullptr) { Select(node->Parent); }
        }
        else if (event.Key == UIKey::Enter && index < visible.size())
        {
            UITreeNode* node = visible[index].first;
            if (node->Children.empty() == false) { SetExpanded(node, node->Expanded == false); }
        }
        else
        {
            return;
        }
        event.Handled = true;
    }

    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override
    {
        const auto visible = VisibleNodes();
        for (std::size_t i = 0u; i < visible.size(); ++i)
        {
            const float rowTop = static_cast<float>(i) * m_RowHeight - GetScrollOffset();
            if (rowTop + m_RowHeight <= 0.0f)
            {
                continue;
            }
            if (rowTop >= GetSize().y)
            {
                break;
            }
            const float y = absolutePosition.y + rowTop;
            const UITreeNode* node = visible[i].first;
            if (node == m_Selected)
            {
                drawList.AddRect(math::Vec2(absolutePosition.x, y),
                    math::Vec2(absolutePosition.x + GetSize().x - (IsScrollBarVisible() == true ? m_ScrollBarThickness : 0.0f), y + m_RowHeight),
                    ApplyVisualColor(math::Vec4(0.22f, 0.38f, 0.64f, 1.0f)));
            }
            if (m_Font != nullptr && m_Font->GetTexture() != nullptr)
            {
                const float x = absolutePosition.x + static_cast<float>(visible[i].second) * m_Indent;
                const std::string label = (node->Children.empty() ? "  " : (node->Expanded ? "v " : "> ")) + node->Text;
                UITextLayoutOptions options{};
                options.Wrap = UITextWrapMode::None;
                m_Font->AppendText(drawList, label, math::Vec2(x, y + m_Baseline), options,
                    ApplyVisualColor(math::Vec4(1.0f, 1.0f, 1.0f, 1.0f)));
            }
        }
        if (IsScrollBarVisible() == true)
        {
            const float left = absolutePosition.x + GetSize().x - m_ScrollBarThickness;
            drawList.AddRect(math::Vec2(left, absolutePosition.y),
                math::Vec2(absolutePosition.x + GetSize().x, absolutePosition.y + GetSize().y),
                ApplyVisualColor(math::Vec4(0.06f, 0.07f, 0.09f, 0.75f)));
            const float top = absolutePosition.y + GetThumbStart();
            drawList.AddRect(math::Vec2(left, top),
                math::Vec2(absolutePosition.x + GetSize().x, top + GetThumbLength()),
                ApplyVisualColor(m_DraggingScrollBar == true
                    ? math::Vec4(0.62f, 0.65f, 0.72f, 0.95f)
                    : math::Vec4(0.42f, 0.45f, 0.52f, 0.95f)));
        }
    }

private:
    UIScrollBarMetrics ScrollMetrics() const
    {
        return UIScrollBarMetrics{ GetSize().y,
            static_cast<float>(VisibleNodes().size()) * m_RowHeight, GetScrollOffset() };
    }

    float GetThumbLength() const { return ScrollMetrics().ThumbLength(); }
    float GetThumbStart() const { return ScrollMetrics().ThumbStart(); }

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

    using VisibleEntry = std::pair<UITreeNode*, std::size_t>;

    bool Contains(const UITreeNode* target) const
    {
        // 解放済みPointerも逆参照せず、現在のTreeのアドレスとだけ比較します。
        std::vector<const UITreeNode*> pending;
        for (const auto& root : m_Roots) { pending.push_back(root.get()); }
        while (pending.empty() == false)
        {
            const UITreeNode* node = pending.back();
            pending.pop_back();
            if (node == target) { return true; }
            for (const auto& child : node->Children) { pending.push_back(child.get()); }
        }
        return false;
    }

    std::vector<VisibleEntry> VisibleNodes() const
    {
        std::vector<VisibleEntry> result;
        std::vector<VisibleEntry> pending;
        for (auto it = m_Roots.rbegin(); it != m_Roots.rend(); ++it)
        {
            pending.emplace_back(it->get(), 0u);
        }
        while (pending.empty() == false)
        {
            auto entry = pending.back();
            pending.pop_back();
            result.push_back(entry);
            if (entry.first->Expanded)
            {
                for (auto it = entry.first->Children.rbegin(); it != entry.first->Children.rend(); ++it)
                {
                    pending.emplace_back(it->get(), entry.second + 1u);
                }
            }
        }
        return result;
    }

    std::vector<std::unique_ptr<UITreeNode>> m_Roots;
    UITreeNode* m_Selected = nullptr;
    Ref<UIFontAtlas> m_Font;
    SelectionHandler m_OnSelectionChanged;
    ExpansionHandler m_OnExpansionChanged;
    float m_RowHeight = 24.0f;
    float m_Indent = 18.0f;
    float m_Baseline = 17.0f;
    float m_ScrollOffset = 0.0f;
    float m_WheelScrollStep = 48.0f;
    float m_ScrollBarThickness = 10.0f;
    float m_DragGrabOffset = 0.0f;
    bool m_DraggingScrollBar = false;
};

} // namespace Raven
