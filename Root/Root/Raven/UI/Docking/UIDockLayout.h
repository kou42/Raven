#pragma once

#include "Raven/UI/Widgets/UITabModel.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace Raven
{

enum class UIDockNodeKind { Tabs, Split };
enum class UIDockSplitAxis { Horizontal, Vertical };

// 描画Widgetとは独立したDockingの論理Treeです。
// UIElementの所有権を変更せず、後続PhaseでUITabViewをLeafへ対応付けます。
class UIDockNode final
{
public:
    using Ptr = std::unique_ptr<UIDockNode>;

    std::uint64_t GetId() const { return m_Id; }
    UIDockNodeKind GetKind() const { return m_Kind; }
    UIDockNode* GetParent() const { return m_Parent; }
    const UIDockNode* GetFirst() const { return m_First.get(); }
    const UIDockNode* GetSecond() const { return m_Second.get(); }
    UIDockSplitAxis GetAxis() const { return m_Axis; }
    float GetSplitRatio() const { return m_Ratio; }
    UITabModel* GetTabs() { return m_Kind == UIDockNodeKind::Tabs ? &m_Tabs : nullptr; }
    const UITabModel* GetTabs() const { return m_Kind == UIDockNodeKind::Tabs ? &m_Tabs : nullptr; }

    // 非有限値や端点を拒否し、ゼロ幅のPaneを作らないようにします。
    bool SetSplitRatio(float ratio)
    {
        if (m_Kind != UIDockNodeKind::Split || std::isfinite(ratio) == false ||
            ratio <= 0.0f || ratio >= 1.0f)
        {
            return false;
        }
        m_Ratio = ratio;
        return true;
    }

private:
    friend class UIDockLayout;
    UIDockNode(std::uint64_t id, UIDockNodeKind kind, UIDockNode* parent)
        : m_Id(id), m_Kind(kind), m_Parent(parent) {}

    std::uint64_t m_Id = 0u;
    UIDockNodeKind m_Kind = UIDockNodeKind::Tabs;
    UIDockNode* m_Parent = nullptr; // 所有権はRoot/親Splitのunique_ptrに限定します。
    UIDockSplitAxis m_Axis = UIDockSplitAxis::Horizontal;
    float m_Ratio = 0.5f;
    Ptr m_First;
    Ptr m_Second;
    UITabModel m_Tabs;
};

// Splitは既存LeafをFirst/Secondのどちらかへ移し、新しい空Leafを追加します。
// IDはLayout内で単調増加し、Tree再構築時にも再利用しません。
class UIDockLayout final
{
public:
    UIDockLayout() : m_Root(UIDockNode::Ptr(new UIDockNode(NextId(), UIDockNodeKind::Tabs, nullptr))) {}

    UIDockNode* GetRoot() { return m_Root.get(); }
    const UIDockNode* GetRoot() const { return m_Root.get(); }

    UIDockNode* FindNode(std::uint64_t id) { return FindRecursive(m_Root.get(), id); }
    const UIDockNode* FindNode(std::uint64_t id) const { return FindRecursive(m_Root.get(), id); }

    // 戻り値は新しい空Tab Leafです。失敗時はTreeとID発行状態を変更しません。
    // 空Leafとその親Splitを畳み、Siblingを同じ位置へ昇格します。
    // Rootは最後のPaneとして残し、IDを再利用しません。
    bool RemoveEmptyLeaf(std::uint64_t leafId)
    {
        UIDockNode* leaf = FindNode(leafId);
        if (leaf == nullptr || leaf->m_Kind != UIDockNodeKind::Tabs ||
            leaf->m_Tabs.GetTabCount() != 0u || leaf->m_Parent == nullptr)
        {
            return false;
        }
        UIDockNode* split = leaf->m_Parent;
        UIDockNode* grandparent = split->m_Parent;
        UIDockNode::Ptr* slot = grandparent == nullptr ? &m_Root :
            (grandparent->m_First.get() == split ?
                &grandparent->m_First : &grandparent->m_Second);
        // 親Splitの残った子を先に確保し、元LeafとSplitをslot代入で解放します。
        UIDockNode::Ptr sibling = split->m_First.get() == leaf ?
            std::move(split->m_Second) : std::move(split->m_First);
        sibling->m_Parent = grandparent;
        *slot = std::move(sibling);
        return true;
    }

    UIDockNode* Split(std::uint64_t leafId, UIDockSplitAxis axis,
        float ratio = 0.5f, bool newLeafFirst = false)
    {
        UIDockNode* leaf = FindNode(leafId);
        if (leaf == nullptr || leaf->m_Kind != UIDockNodeKind::Tabs ||
            std::isfinite(ratio) == false || ratio <= 0.0f || ratio >= 1.0f ||
            m_NextId > UINT64_MAX - 3u)
        {
            return nullptr;
        }

        UIDockNode::Ptr* slot = leaf->m_Parent == nullptr ? &m_Root :
            (leaf->m_Parent->m_First.get() == leaf ?
                &leaf->m_Parent->m_First : &leaf->m_Parent->m_Second);
        UIDockNode* parent = leaf->m_Parent;
        auto split = UIDockNode::Ptr(new UIDockNode(NextId(), UIDockNodeKind::Split, parent));
        auto fresh = UIDockNode::Ptr(new UIDockNode(NextId(), UIDockNodeKind::Tabs, split.get()));
        UIDockNode* freshRaw = fresh.get();
        split->m_Axis = axis;
        split->m_Ratio = ratio;

        // 古いLeafを破棄せず所有権だけを移し、Tab/選択状態を保持します。
        UIDockNode::Ptr existing = std::move(*slot);
        existing->m_Parent = split.get();
        if (newLeafFirst == true)
        {
            split->m_First = std::move(fresh);
            split->m_Second = std::move(existing);
        }
        else
        {
            split->m_First = std::move(existing);
            split->m_Second = std::move(fresh);
        }
        *slot = std::move(split);
        return freshRaw;
    }

private:
    std::uint64_t NextId() { return m_NextId++; }

    static UIDockNode* FindRecursive(UIDockNode* node, std::uint64_t id)
    {
        if (node == nullptr)
        {
            return nullptr;
        }
        if (node->m_Id == id)
        {
            return node;
        }
        if (UIDockNode* first = FindRecursive(node->m_First.get(), id))
        {
            return first;
        }
        return FindRecursive(node->m_Second.get(), id);
    }

    std::uint64_t m_NextId = 1u;
    UIDockNode::Ptr m_Root;
};

} // namespace Raven
