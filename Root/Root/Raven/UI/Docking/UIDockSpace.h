#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Docking/UIDockGeometry.h"
#include "Raven/UI/Widgets/UISplitter.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Raven
{

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

    // Tree変更後は本API経由でWidgetを生成・再配置します。
    UIDockNode* Split(std::uint64_t leafId, UIDockSplitAxis axis,
        float ratio = 0.5f, bool newLeafFirst = false);
    void RefreshLayout();
    void SetSplitterThickness(float value);
    void SetMinimumPaneExtent(float value);

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    void SyncWidgets();
    void ApplyLayout();
    void OnSplitterDrag(std::uint64_t splitId, float delta);
    static void ApplyRect(UIElement& element, const UIDockRect& rect);

    UIDockLayout m_Layout;
    std::unordered_map<std::uint64_t, UIElement*> m_Panes;
    std::unordered_map<std::uint64_t, UISplitter*> m_Splitters;
    float m_SplitterThickness = 5.0f;
    float m_MinimumPaneExtent = 32.0f;
};

} // namespace Raven
