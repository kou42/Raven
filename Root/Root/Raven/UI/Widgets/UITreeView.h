#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"

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
// Scroll/仮想化/複数選択は別段階とし、現段階は単一選択と展開操作に限定します。
class UITreeView final : public UIElement
{
public:
    using SelectionHandler = std::function<void(std::uint64_t)>;
    using ExpansionHandler = std::function<void(std::uint64_t, bool)>;

    UITreeView()
    {
        SetFocusable(true);
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
        // Callbackへ渡したNode*も無効になるので、先に選択を解除します。
        Select(nullptr);
        m_Roots.clear();
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
        if (m_OnSelectionChanged)
        {
            m_OnSelectionChanged(node != nullptr ? node->Id : 0u);
        }
        return true;
    }

    UITreeNode* GetSelectedNode() const { return m_Selected; }
    void SetOnSelectionChanged(SelectionHandler handler) { m_OnSelectionChanged = std::move(handler); }
    void SetOnExpansionChanged(ExpansionHandler handler) { m_OnExpansionChanged = std::move(handler); }
    void SetFont(const Ref<UIFontAtlas>& font) { m_Font = font; }
    void SetRowHeight(float value)
    {
        if (std::isfinite(value) && value > 0.0f)
        {
            m_RowHeight = value;
            InvalidateMeasure();
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
        node->Expanded = expanded;
        InvalidateMeasure();
        if (m_OnExpansionChanged)
        {
            m_OnExpansionChanged(node->Id, expanded);
        }
        return true;
    }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
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
        const auto visible = VisibleNodes();
        const std::size_t index = static_cast<std::size_t>(local.y / m_RowHeight);
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
            const float y = absolutePosition.y + static_cast<float>(i) * m_RowHeight;
            if (static_cast<float>(i) * m_RowHeight >= GetSize().y)
            {
                break;
            }
            const UITreeNode* node = visible[i].first;
            if (node == m_Selected)
            {
                drawList.AddRect(math::Vec2(absolutePosition.x, y),
                    math::Vec2(absolutePosition.x + GetSize().x, y + m_RowHeight),
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
    }

private:
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
};

} // namespace Raven
