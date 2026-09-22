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
    using NodeDroppedHandler = std::function<void(std::uint64_t, std::uint64_t)>;
    enum class DropPlacement { Child, Before, After, RootEnd };
    // RootEndではtargetId=0を渡します。既存のNodeDroppedHandlerも同じ規約です。
    using NodePlacedHandler = std::function<void(std::uint64_t, std::uint64_t, DropPlacement)>;

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
        if (GetContext() != nullptr && GetContext()->GetDragSource() == this)
        {
            GetContext()->CancelDrag();
        }
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
    // 同一Tree内の子・前後・Root末尾への移動を有効にします。初期状態では既存Tree操作に影響しません。
    void SetNodeDragDropEnabled(bool value)
    {
        if (value == false && GetContext() != nullptr &&
            GetContext()->GetDragSource() == this)
        {
            GetContext()->CancelDrag();
        }
        m_NodeDragDropEnabled = value;
    }
    bool IsNodeDragDropEnabled() const { return m_NodeDragDropEnabled; }
    // 別TreeView間の所有権移動は明示的に許可した受入側でのみ有効にします。
    void SetExternalNodeDropEnabled(bool value) { m_ExternalNodeDropEnabled = value; }
    bool IsExternalNodeDropEnabled() const { return m_ExternalNodeDropEnabled; }
    // Drag中のPointer Moveとフレーム更新の両方で端付近をスクロールします。
    // Child位置で折りたたみNodeに一定時間Hoverすると展開します。
    void SetDragAutoExpandEnabled(bool value)
    {
        m_DragAutoExpandEnabled = value;
        if (value == false)
        {
            ResetDragAutoExpand();
        }
    }
    bool IsDragAutoExpandEnabled() const { return m_DragAutoExpandEnabled; }
    void SetDragAutoExpandDelay(float seconds)
    {
        if (std::isfinite(seconds) && seconds > 0.0f)
        {
            m_DragAutoExpandDelay = seconds;
        }
    }
    void SetDragAutoScrollEnabled(bool value) { m_DragAutoScrollEnabled = value; }
    bool IsDragAutoScrollEnabled() const { return m_DragAutoScrollEnabled; }
    void SetDragAutoScrollEdge(float value)
    {
        if (std::isfinite(value) && value > 0.0f)
        {
            m_DragAutoScrollEdge = value;
        }
    }
    void SetDragAutoScrollSpeed(float value)
    {
        if (std::isfinite(value) && value > 0.0f)
        {
            m_DragAutoScrollSpeed = value;
        }
    }
    void SetDragAutoScrollStep(float value)
    {
        if (std::isfinite(value) && value > 0.0f)
        {
            m_DragAutoScrollStep = value;
        }
    }
    void SetOnNodeDropped(NodeDroppedHandler handler) { m_OnNodeDropped = std::move(handler); }
    void SetOnNodePlaced(NodePlacedHandler handler) { m_OnNodePlaced = std::move(handler); }
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
        if (event.Type == UIMouseEventType::Cancel ||
            (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left))
        {
            m_PendingNodeId = 0u;
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
            // 選択CallbackがNodeを削除・移動しても古いPointerを逆参照しません。
            const std::uint64_t nodeId = node->Id;
            Select(node);
            UITreeNode* currentNode = FindNode(nodeId);
            if (m_NodeDragDropEnabled == true && event.Context != nullptr &&
                nodeId != 0u && currentNode != nullptr)
            {
                m_PendingNodeId = nodeId;
                // Down時点でCaptureし、PointerがTree外へ出てもMoveを受け取ります。
                // 閾値未満のUpはUIContextが通常Clickとして扱います。
                if (event.Context->BeginDrag(this,
                    UIDragDropPayload{ "Raven/UITreeNode", std::to_string(nodeId) },
                    event.ScreenPosition) == true)
                {
                    event.Context->SetDragPreview(currentNode->Text, m_Font);
                }
            }
        }
        event.Handled = true;
    }

    bool OnDragDropEvent(UIDragDropEvent& event) override
    {
        if (event.Type == UIDragDropEventType::End || event.Type == UIDragDropEventType::Cancel ||
            event.Type == UIDragDropEventType::Leave)
        {
            m_PendingNodeId = 0u;
            ResetDragAutoExpand();
            return false;
        }
        // Drop時と無効なOverではHoverの蓄積時間を破棄します。
        if (event.Type != UIDragDropEventType::Over)
        {
            ResetDragAutoExpand();
        }
        if (m_NodeDragDropEnabled == false || event.Payload == nullptr ||
            event.Payload->Type != "Raven/UITreeNode")
        {
            ResetDragAutoExpand();
            return false;
        }
        UITreeView* sourceView = dynamic_cast<UITreeView*>(event.Source);
        if (sourceView == nullptr || sourceView->m_NodeDragDropEnabled == false ||
            (sourceView != this && m_ExternalNodeDropEnabled == false))
        {
            ResetDragAutoExpand();
            return false;
        }
        // Source所属の全Nodeを調べるため、折りたたみ中のNodeも識別できます。
        // IDは数値変換せず照合し、外部からの不正なPayloadで例外を発生させません。
        UITreeNode* source = nullptr;
        std::vector<UITreeNode*> pending;
        for (const auto& root : sourceView->m_Roots)
        {
            pending.push_back(root.get());
        }
        while (pending.empty() == false)
        {
            UITreeNode* candidate = pending.back();
            pending.pop_back();
            if (std::to_string(candidate->Id) == event.Payload->Data)
            {
                source = candidate;
                break;
            }
            for (const auto& child : candidate->Children)
            {
                pending.push_back(child.get());
            }
        }
        math::Vec2 local;
        if (TryScreenToLocalPosition(event.ScreenPosition, local) == false ||
            local.x < 0.0f || local.y < 0.0f ||
            local.x >= GetSize().x || local.y >= GetSize().y ||
            (IsScrollBarVisible() == true && local.x >= GetSize().x - m_ScrollBarThickness))
        {
            ResetDragAutoExpand();
            return false;
        }
        // 端付近のMoveで先にScrollし、その後のHit判定を新しい表示行に合わせます。
        // 無効なPayloadでTreeがスクロールしないようSource確認後に実行します。
        if (source == nullptr)
        {
            ResetDragAutoExpand();
            return false;
        }
        if (event.Type == UIDragDropEventType::Over &&
            m_DragAutoScrollEnabled == true && GetMaxScrollOffset() > 0.0f)
        {
            const float edge = std::min(m_DragAutoScrollEdge, GetSize().y * 0.5f);
            const float previous = GetScrollOffset();
            if (local.y < edge)
            {
                SetScrollOffset(previous - (event.DeltaSeconds > 0.0f
                    ? m_DragAutoScrollSpeed * event.DeltaSeconds : m_DragAutoScrollStep));
            }
            else if (local.y >= GetSize().y - edge)
            {
                SetScrollOffset(previous + (event.DeltaSeconds > 0.0f
                    ? m_DragAutoScrollSpeed * event.DeltaSeconds : m_DragAutoScrollStep));
            }
        }
        UITreeNode* target = NodeAt(event.ScreenPosition);
        // 最終行より下の空白はRoot末尾への挿入先として扱います。
        // 空のTreeViewにも、既存Rootを持つTreeViewにもDropできます。
        const float rowPosition = std::fmod(local.y + GetScrollOffset(), m_RowHeight);
        const DropPlacement placement = target == nullptr ? DropPlacement::RootEnd
            : (rowPosition < m_RowHeight * 0.25f ? DropPlacement::Before
                : (rowPosition >= m_RowHeight * 0.75f ? DropPlacement::After : DropPlacement::Child));
        UITreeNode* newParent = placement == DropPlacement::Child ? target
            : (target != nullptr ? target->Parent : nullptr);
        // 移動先の親がSource自身または子孫なら循環するため拒否します。
        for (UITreeNode* ancestor = newParent; ancestor != nullptr; ancestor = ancestor->Parent)
        {
            if (ancestor == source)
            {
                ResetDragAutoExpand();
                return false;
            }
        }
        if (source == target)
        {
            ResetDragAutoExpand();
            return false;
        }
        // 同じ兄弟列の隣接位置へDropしても並び順は変わりません。
        // 不要なRemove/InsertとCallbackを避け、Scene側のUndo履歴も汚しません。
        if (sourceView == this && placement != DropPlacement::Child &&
            placement != DropPlacement::RootEnd && source->Parent == target->Parent)
        {
            const auto& siblings = source->Parent != nullptr ? source->Parent->Children : m_Roots;
            const auto sourceIt = std::find_if(siblings.begin(), siblings.end(),
                [source](const auto& item) { return item.get() == source; });
            const auto targetIt = std::find_if(siblings.begin(), siblings.end(),
                [target](const auto& item) { return item.get() == target; });
            if (sourceIt != siblings.end() && targetIt != siblings.end() &&
                ((placement == DropPlacement::Before && sourceIt + 1 == targetIt) ||
                    (placement == DropPlacement::After && targetIt + 1 == sourceIt)))
            {
                ResetDragAutoExpand();
                return false;
            }
        }
        // ChildへのDropは子リスト末尾への追加です。既に末尾の子なら並び順は変わりません。
        // AutoExpandの待機状態も解除し、無変更Dropで展開や通知を発生させません。
        if (placement == DropPlacement::Child && sourceView == this &&
            source->Parent == target && target->Children.empty() == false &&
            target->Children.back().get() == source)
        {
            ResetDragAutoExpand();
            return false;
        }
        if (placement == DropPlacement::RootEnd && sourceView == this &&
            source->Parent == nullptr && m_Roots.back().get() == source)
        {
            // 最後尾のRootを同じ位置へDropするだけなら受け入れません。
            ResetDragAutoExpand();
            return false;
        }
        if (sourceView != this)
        {
            // 移動するSubtree全体のIDを検査し、受入先のFindNode一意性を維持します。
            std::vector<const UITreeNode*> subtree{ source };
            while (subtree.empty() == false)
            {
                const UITreeNode* candidate = subtree.back();
                subtree.pop_back();
                if (FindNode(candidate->Id) != nullptr)
                {
                    ResetDragAutoExpand();
                    return false;
                }
                for (const auto& child : candidate->Children)
                {
                    subtree.push_back(child.get());
                }
            }
        }
        if (event.Type == UIDragDropEventType::Over)
        {
            m_DropPointerPosition = event.ScreenPosition;
            m_DropPlacement = placement;
            event.Accepted = true;
            // IDでHover対象を記録し、Node*の破棄後に参照しないようにします。
            const bool expandable = m_DragAutoExpandEnabled == true &&
                placement == DropPlacement::Child && target != nullptr &&
                target->Expanded == false && target->Children.empty() == false;
            if (expandable == false)
            {
                ResetDragAutoExpand();
            }
            else
            {
                if (m_DragAutoExpandTracking == false || m_DragAutoExpandNodeId != target->Id)
                {
                    m_DragAutoExpandNodeId = target->Id;
                    m_DragAutoExpandTracking = true;
                    m_DragAutoExpandElapsed = 0.0f;
                }
                m_DragAutoExpandElapsed += std::max(0.0f, event.DeltaSeconds);
                if (m_DragAutoExpandElapsed >= m_DragAutoExpandDelay)
                {
                    const std::uint64_t expandId = target->Id;
                    ResetDragAutoExpand();
                    // 通常の展開APIを通し、MeasureとExpansion callbackを更新します。
                    UITreeNode* expandNode = FindNode(expandId);
                    if (expandNode != nullptr)
                    {
                        SetExpanded(expandNode, true);
                    }
                }
            }
            return true;
        }
        if (event.Type == UIDragDropEventType::Drop)
        {
            ResetDragAutoExpand();
            auto& oldSiblings = source->Parent != nullptr ? source->Parent->Children : sourceView->m_Roots;
            auto oldIt = std::find_if(oldSiblings.begin(), oldSiblings.end(),
                [source](const auto& item) { return item.get() == source; });
            if (oldIt == oldSiblings.end())
            {
                ResetDragAutoExpand();
                return false;
            }
            // 所有権移動前に選択の所属を記録します。移動後のParent chainでは判定できません。
            bool clearSourceSelection = false;
            if (sourceView != this)
            {
                for (UITreeNode* selected = sourceView->m_Selected; selected != nullptr;
                    selected = selected->Parent)
                {
                    if (selected == source)
                    {
                        clearSourceSelection = true;
                        break;
                    }
                }
            }
            // 同じ兄弟配列内で移動する場合も、先に抜いてから挿入位置を探します。
            std::unique_ptr<UITreeNode> moved = std::move(*oldIt);
            oldSiblings.erase(oldIt);
            auto& newSiblings = newParent != nullptr ? newParent->Children : m_Roots;
            moved->Parent = newParent;
            if (placement == DropPlacement::Child || placement == DropPlacement::RootEnd)
            {
                newSiblings.push_back(std::move(moved));
                if (target != nullptr)
                {
                    target->Expanded = true;
                }
            }
            else
            {
                auto targetIt = std::find_if(newSiblings.begin(), newSiblings.end(),
                    [target](const auto& item) { return item.get() == target; });
                if (targetIt == newSiblings.end())
                {
                    // Tree内で不整合が起きた場合も所有権を失わないよう末尾へ退避します。
                    newSiblings.push_back(std::move(moved));
                    ResetDragAutoExpand();
                    return false;
                }
                newSiblings.insert(placement == DropPlacement::After ? targetIt + 1 : targetIt,
                    std::move(moved));
            }
            const std::uint64_t sourceId = source->Id;
            const std::uint64_t targetId = target != nullptr ? target->Id : 0u;
            if (sourceView != this)
            {
                // Source側の選択が移動Subtreeを指していたら、無効な選択Pointerを残しません。
                if (clearSourceSelection == true)
                {
                    sourceView->Select(nullptr);
                }
                sourceView->InvalidateMeasure();
                sourceView->SetScrollOffset(sourceView->m_ScrollOffset);
            }
            InvalidateMeasure();
            EnsureSelectedVisible();
            if (m_OnNodeDropped)
            {
                m_OnNodeDropped(sourceId, targetId);
            }
            if (m_OnNodePlaced)
            {
                m_OnNodePlaced(sourceId, targetId, placement);
            }
            return true;
        }
        return false;
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
            const UIContext* context = GetContext();
            if (context != nullptr && context->IsDragging() == true &&
                context->GetDropTarget() == this && NodeAt(m_DropPointerPosition) == node)
            {
                const float right = absolutePosition.x + GetSize().x -
                    (IsScrollBarVisible() == true ? m_ScrollBarThickness : 0.0f);
                if (m_DropPlacement == DropPlacement::Child)
                {
                    drawList.AddRect(math::Vec2(absolutePosition.x, y),
                        math::Vec2(right, y + m_RowHeight),
                        ApplyVisualColor(math::Vec4(0.18f, 0.56f, 0.32f, 0.55f)));
                }
                else
                {
                    // Before/Afterは行全体ではなく境界線で挿入位置を示します。
                    const float lineY = m_DropPlacement == DropPlacement::Before ? y : y + m_RowHeight;
                    // Viewport境界に一致する線はClipで完全に消えるため、内側へ寄せます。
                    const float indicatorY = std::clamp(lineY,
                        absolutePosition.y + 1.5f,
                        absolutePosition.y + std::max(1.5f, GetSize().y - 1.5f));
                    drawList.AddRect(math::Vec2(absolutePosition.x, indicatorY - 1.5f),
                        math::Vec2(right, indicatorY + 1.5f),
                        ApplyVisualColor(math::Vec4(0.42f, 0.90f, 0.57f, 0.95f)));
                }
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
        const UIContext* context = GetContext();
        if (context != nullptr && context->IsDragging() == true &&
            context->GetDropTarget() == this && m_DropPlacement == DropPlacement::RootEnd)
        {
            // 空白へのDropはRoot末尾への挿入線で表し、ChildへのDropと区別します。
            const float lineY = absolutePosition.y +
                static_cast<float>(visible.size()) * m_RowHeight - GetScrollOffset();
            // 空Treeの先頭・Scroll末尾とも線の全幅がViewport内に残るよう補正します。
            const float indicatorY = std::clamp(lineY,
                absolutePosition.y + 1.5f,
                absolutePosition.y + std::max(1.5f, GetSize().y - 1.5f));
            const float right = absolutePosition.x + GetSize().x -
                (IsScrollBarVisible() == true ? m_ScrollBarThickness : 0.0f);
            drawList.AddRect(math::Vec2(absolutePosition.x, indicatorY - 1.5f),
                math::Vec2(right, indicatorY + 1.5f),
                ApplyVisualColor(math::Vec4(0.42f, 0.90f, 0.57f, 0.95f)));
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
    void ResetDragAutoExpand()
    {
        m_DragAutoExpandNodeId = 0u;
        m_DragAutoExpandElapsed = 0.0f;
        m_DragAutoExpandTracking = false;
    }

    UITreeNode* NodeAt(const math::Vec2& screenPosition) const
    {
        math::Vec2 local;
        if (TryScreenToLocalPosition(screenPosition, local) == false ||
            local.x < 0.0f || local.y < 0.0f || local.x >= GetSize().x ||
            local.y >= GetSize().y ||
            (IsScrollBarVisible() == true && local.x >= GetSize().x - m_ScrollBarThickness))
        {
            return nullptr;
        }
        const auto visible = VisibleNodes();
        const std::size_t index = static_cast<std::size_t>((local.y + GetScrollOffset()) / m_RowHeight);
        return index < visible.size() ? visible[index].first : nullptr;
    }

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
    NodeDroppedHandler m_OnNodeDropped;
    NodePlacedHandler m_OnNodePlaced;
    DropPlacement m_DropPlacement = DropPlacement::Child;
    std::uint64_t m_PendingNodeId = 0u;
    bool m_NodeDragDropEnabled = false;
    bool m_ExternalNodeDropEnabled = false;
    bool m_DragAutoScrollEnabled = true;
    bool m_DragAutoExpandEnabled = true;
    bool m_DragAutoExpandTracking = false;
    std::uint64_t m_DragAutoExpandNodeId = 0u;
    float m_DragAutoExpandElapsed = 0.0f;
    float m_DragAutoExpandDelay = 0.65f;
    float m_DragAutoScrollEdge = 24.0f;
    float m_DragAutoScrollStep = 12.0f;
    float m_DragAutoScrollSpeed = 240.0f;
    math::Vec2 m_DropPointerPosition{};
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
