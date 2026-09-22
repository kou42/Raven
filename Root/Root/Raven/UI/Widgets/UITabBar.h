#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Widgets/UITabModel.h"

#include <cstdint>

namespace Raven
{

// UITabModelの表示・入力担当。モデルの所有権は呼び出し側が持ちます。
// 固定幅Tabを横一列に描画し、Overflow時は横スクロールとClipで表示範囲を制御します。
// Drag並び替えは既存UIContextのDrag & Drop経路を使用します。
class UITabBar final : public UIElement
{
public:
    explicit UITabBar(UITabModel& model);

    void SetDragReorderEnabled(bool enabled) { m_DragReorderEnabled = enabled; }
    bool IsDragReorderEnabled() const { return m_DragReorderEnabled; }

    void SetScrollOffset(float offset);
    float GetScrollOffset() const { return m_ScrollOffset; }
    float GetMaxScrollOffset() const;
    void EnsureTabVisible(std::uint64_t id);

    void SetFont(const Ref<UIFontAtlas>& font);
    void SetTabWidth(float width);
    void SetTabHeight(float height);
    float GetTabWidth() const { return m_TabWidth; }
    float GetTabHeight() const { return m_TabHeight; }

protected:
    math::Vec2 OnMeasureContent() const override;
    void OnMouseEvent(UIMouseEvent& event) override;
    bool OnDragDropEvent(UIDragDropEvent& event) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    // Close領域はTab右端の固定幅です。ID=0はHitなしを表します。
    std::uint64_t HitTab(float x, float y, bool& close) const;

    UITabModel& m_Model;
    Ref<UIFontAtlas> m_Font;
    float m_TabWidth = 148.0f;
    float m_TabHeight = 30.0f;
    float m_CloseWidth = 26.0f;
    float m_ScrollOffset = 0.0f;
    std::uint64_t m_HoveredId = 0u;
    std::uint64_t m_PendingDragId = 0u;
    std::size_t m_DropIndex = 0u;
    bool m_DropIndicatorVisible = false;
    bool m_DragReorderEnabled = true;
    bool m_HoveredClose = false;
};

} // namespace Raven
