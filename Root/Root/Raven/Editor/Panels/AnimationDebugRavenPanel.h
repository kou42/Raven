#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Editor/Panels/AnimationDebugSnapshot.h"

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace Raven
{

class UIContext;
class UIElement;
class UIFontAtlas;
class UITable;

// AnimationDebugSnapshotだけを表示する読み取り専用Raven UI Panelです。
// 可変長診断値は単一UITableのDataSourceへ平坦化し、行数に比例したWidget生成を避けます。
class AnimationDebugRavenPanel
{
public:
    bool Attach(UIContext& context, const Ref<UIFontAtlas>& font);
    void Detach();
    void Update(const AnimationDebugSnapshot& snapshot);
    void SetVisible(bool visible);

    bool IsAttached() const;
    bool IsVisible() const;
    std::size_t GetDisplayRowCount() const;
    std::size_t GetSelectedBoneIndex() const;

private:
    struct DisplayRow
    {
        std::string Section;
        std::string Item;
        std::string Value;
        std::size_t BoneIndex = std::numeric_limits<std::size_t>::max();
    };

    void RebuildRows();
    void AddRow(std::string section, std::string item, std::string value);
    void AddBoneRow(std::size_t boneIndex, std::string name, bool selected);
    std::string GetCellText(std::size_t row, std::size_t column) const;
    void OnRowSelected(std::size_t row);

    UIContext* m_Context = nullptr;
    UIElement* m_Root = nullptr;
    UITable* m_Table = nullptr;
    AnimationDebugSnapshot m_Snapshot;
    std::vector<DisplayRow> m_Rows;
    std::size_t m_SelectedBoneIndex = 0u;
};

} // namespace Raven
