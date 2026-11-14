#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Editor/Panels/DiagnosticsViewModel.h"

namespace Raven
{
class UIContext;
class UIElement;
class UIFontAtlas;
class UILabel;
class UITable;
class UIComboBox;

// WidgetはUIContext所有。CollectorにはStoreだけを渡し、Panelの寿命と分離します。
class DiagnosticsRavenPanel final
{
public:
    bool Attach(UIContext& context, const Ref<UIFontAtlas>& font,
        std::shared_ptr<Diagnostics::DiagnosticEventStore> store = nullptr);
    void Detach();
    void Update(float deltaSeconds);
    void SetVisible(bool visible);
    bool IsAttached() const;
    UIElement* GetRootElement() const;
    const DiagnosticsViewModel& GetModel() const;
private:
    void Refresh();
    std::string Cell(std::size_t row, std::size_t column) const;
    UIElement* m_Root = nullptr;
    UILabel* m_Status = nullptr;
    UILabel* m_Detail = nullptr;
    UILabel* m_TimelineInfo = nullptr;
    UITable* m_Table = nullptr;
    UIComboBox* m_Counters = nullptr;
    std::shared_ptr<Diagnostics::DiagnosticEventStore> m_Store;
    DiagnosticsViewModel m_Model;
    float m_Elapsed = 1.0f;
    bool m_Frozen = false;
    bool m_Updating = false;
    std::string m_FilterError;
};
}
