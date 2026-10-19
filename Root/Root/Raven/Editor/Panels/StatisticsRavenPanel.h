#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Editor/Panels/StatisticsSnapshot.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Raven
{

class UIContext;
class UICheckbox;
class UIElement;
class UIFontAtlas;
class UILabel;
class UITable;

// StatisticsSnapshot表示と、明示的なCPU Profiler Actionを提供するRaven UI Panelです。
// Scene / Profiler / Rendererへの参照は保持せず、Widget TreeのLifetimeだけを管理します。
class StatisticsRavenPanel
{
public:
    bool Attach(UIContext& context, const Ref<UIFontAtlas>& font);
    void Detach();
    void Update(const StatisticsSnapshot& snapshot);
    void SetVisible(bool visible);

    bool IsAttached() const;
    bool IsVisible() const;
    UIElement* GetRootElement() const { return m_Root; }
    std::size_t GetProfileRowCount() const;
    std::size_t GetCounterRowCount() const;

private:
    UILabel* AddLabel(UIElement& parent, float y, const Ref<UIFontAtlas>& font);
    void OnProfilerEnabledToggled(bool enabled);
    void UpdateSummaryLabels();
    std::string GetProfileCellText(std::size_t row, std::size_t column) const;
    std::string GetCounterCellText(std::size_t row, std::size_t column) const;

    UIContext* m_Context = nullptr;
    UIElement* m_Root = nullptr;
    std::vector<UILabel*> m_SummaryLabels;
    UICheckbox* m_ProfilerEnabledCheckbox = nullptr;
    UILabel* m_ActionStatusLabel = nullptr;
    UITable* m_ProfileTable = nullptr;
    UITable* m_CounterTable = nullptr;
    StatisticsSnapshot m_Snapshot;
};

} // namespace Raven
