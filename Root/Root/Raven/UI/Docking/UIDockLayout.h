#pragma once

#include "Raven/UI/Widgets/UITabModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Raven
{

enum class UIDockNodeKind { Tabs, Split };
enum class UIDockSplitAxis { Horizontal, Vertical };

struct UIDockLayoutRecord
{
    std::uint64_t Id = 0u;
    UIDockNodeKind Kind = UIDockNodeKind::Tabs;
    UIDockSplitAxis Axis = UIDockSplitAxis::Horizontal;
    float Ratio = 0.5f;
    std::uint32_t Depth = 0u;
};

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

    // Preorder幾何Snapshot。TabのContentは保存しません。
    std::vector<UIDockLayoutRecord> SaveStructure() const
    {
        std::vector<UIDockLayoutRecord> records;
        SaveRecursive(m_Root.get(), 0u, records);
        return records;
    }

    // 全Recordを検証してから置換。Tabを持つTreeはContent孤立防止のため拒否します。
    bool RestoreStructure(const std::vector<UIDockLayoutRecord>& records)
    {
        if (records.empty() || records.size() > 4096u ||
            HasTabsRecursive(m_Root.get()) == true)
        {
            return false;
        }
        std::unordered_set<std::uint64_t> ids;
        std::size_t cursor = 0u;
        std::uint64_t maximumId = 0u;
        UIDockNode::Ptr root = LoadRecursive(records, cursor, 0u, nullptr, ids, maximumId);
        if (root == nullptr || cursor != records.size() || maximumId == UINT64_MAX)
        {
            return false;
        }
        m_Root = std::move(root);
        m_NextId = maximumId + 1u;
        return true;
    }

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

    static bool HasTabsRecursive(const UIDockNode* node)
    {
        return node != nullptr && (node->m_Tabs.GetTabCount() != 0u ||
            HasTabsRecursive(node->m_First.get()) ||
            HasTabsRecursive(node->m_Second.get()));
    }

    static void SaveRecursive(const UIDockNode* node, std::uint32_t depth,
        std::vector<UIDockLayoutRecord>& records)
    {
        if (node == nullptr)
        {
            return;
        }
        records.push_back({ node->m_Id, node->m_Kind, node->m_Axis, node->m_Ratio, depth });
        SaveRecursive(node->m_First.get(), depth + 1u, records);
        SaveRecursive(node->m_Second.get(), depth + 1u, records);
    }

    static UIDockNode::Ptr LoadRecursive(const std::vector<UIDockLayoutRecord>& records,
        std::size_t& cursor, std::uint32_t depth, UIDockNode* parent,
        std::unordered_set<std::uint64_t>& ids, std::uint64_t& maximumId)
    {
        if (cursor >= records.size() || depth >= 4096u)
        {
            return nullptr;
        }
        const UIDockLayoutRecord& record = records[cursor];
        if (record.Depth != depth || record.Id == 0u ||
            ids.insert(record.Id).second == false ||
            (record.Kind != UIDockNodeKind::Tabs && record.Kind != UIDockNodeKind::Split) ||
            (record.Axis != UIDockSplitAxis::Horizontal &&
                record.Axis != UIDockSplitAxis::Vertical) ||
            std::isfinite(record.Ratio) == false ||
            record.Ratio <= 0.0f || record.Ratio >= 1.0f)
        {
            return nullptr;
        }
        ++cursor;
        UIDockNode::Ptr node(new UIDockNode(record.Id, record.Kind, parent));
        node->m_Axis = record.Axis;
        node->m_Ratio = record.Ratio;
        maximumId = std::max(maximumId, record.Id);
        if (record.Kind == UIDockNodeKind::Split)
        {
            node->m_First = LoadRecursive(records, cursor, depth + 1u,
                node.get(), ids, maximumId);
            if (node->m_First == nullptr)
            {
                return nullptr;
            }
            node->m_Second = LoadRecursive(records, cursor, depth + 1u,
                node.get(), ids, maximumId);
            if (node->m_Second == nullptr)
            {
                return nullptr;
            }
        }
        return node;
    }

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
