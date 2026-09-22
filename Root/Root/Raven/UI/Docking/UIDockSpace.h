#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Docking/UIDockGeometry.h"
#include "Raven/UI/Widgets/UISplitter.h"
#include "Raven/UI/Widgets/UITabView.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Raven
{

// UIElementは永続化できないため、復元時はTab IDからContentを再生成します。
struct UIDockTabRecord
{
    std::uint64_t LeafId = 0u;
    UITabItem Tab;
};

struct UIDockSpaceSnapshot
{
    std::vector<UIDockLayoutRecord> Structure;
    std::vector<UIDockTabRecord> Tabs;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> Selections;
};

// Docking論理Treeの配置結果をRetained UI Treeへ反映するHostです。
// PaneのUIElement所有権はDockSpaceが持ち、論理NodeはIDだけで対応付けます。
class UIDockSpace final : public UIElement
{
public:
    UIDockSpace();

    UIDockLayout& GetLayout() { return m_Layout; }
    const UIDockLayout& GetLayout() const { return m_Layout; }

    // Leaf一つにつきPane一つを登録します。既存Paneを上書き・破棄しません。
    bool SetPane(std::uint64_t leafId, Scope<UIElement> pane);
    UIElement* GetPane(std::uint64_t leafId) const;
    UISplitter* GetSplitter(std::uint64_t splitId) const;

    // DockSpaceが生成したTabViewのみを管理し、Tab操作を論理Modelへ同期します。
    UITabView* CreateTabView(std::uint64_t leafId);
    UITabView* GetTabView(std::uint64_t leafId) const;
    bool AddTab(std::uint64_t leafId, std::uint64_t tabId, std::string title,
        Scope<UIElement> content, bool closable = true);
    bool SelectTab(std::uint64_t leafId, std::uint64_t tabId);
    bool CloseTab(std::uint64_t leafId, std::uint64_t tabId);
    bool MoveTab(std::uint64_t leafId, std::uint64_t tabId, std::size_t index);
    bool MoveTabToPane(std::uint64_t sourceLeafId, std::uint64_t targetLeafId,
        std::uint64_t tabId);

    // Tree変更後は本API経由でWidgetを生成・再配置します。
    UIDockNode* Split(std::uint64_t leafId, UIDockSplitAxis axis,
        float ratio = 0.5f, bool newLeafFirst = false);
    // Tabが空のLeafだけを閉じ、Siblingを昇格させます。Rootは閉じません。
    bool CloseEmptyPane(std::uint64_t leafId);
    // UI Pane未生成のDockSpaceへ幾何Snapshotを復元します。
    bool RestoreStructure(const std::vector<UIDockLayoutRecord>& records);
    UIDockSpaceSnapshot SaveSnapshot() const;
    // 空DockSpace専用。Factoryが全Contentを生成できた場合のみ復元を開始します。
    using ContentFactory = std::function<Scope<UIElement>(std::uint64_t, const UITabItem&)>;
    bool RestoreSnapshot(const UIDockSpaceSnapshot& snapshot, const ContentFactory& factory);
    void RefreshLayout();
    void SetSplitterThickness(float value);
    void SetMinimumPaneExtent(float value);

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;
    bool OnDragDropEvent(UIDragDropEvent& event) override;

private:
    void SyncWidgets();
    void ApplyLayout();
    void OnSplitterDrag(std::uint64_t splitId, float delta);
    static void ApplyRect(UIElement& element, const UIDockRect& rect);
    std::uint64_t FindLeafAt(const math::Vec2& local) const;
    std::uint64_t FindSourceLeaf(const UIElement* source) const;

    UIDockLayout m_Layout;
    std::unordered_map<std::uint64_t, UIElement*> m_Panes;
    std::unordered_map<std::uint64_t, UITabView*> m_TabViews;
    std::unordered_map<std::uint64_t, UISplitter*> m_Splitters;
    UIElement* m_Preview = nullptr;
    std::uint64_t m_PreviewLeaf = 0u;
    float m_SplitterThickness = 5.0f;
    float m_MinimumPaneExtent = 32.0f;
};

} // namespace Raven
