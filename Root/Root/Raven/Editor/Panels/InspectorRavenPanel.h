#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Editor/Panels/InspectorSnapshot.h"

#include <string>
#include <vector>

namespace Raven
{

class UIContext;
class UIElement;
class UIFontAtlas;
class UILabel;
class UICollapsibleSection;

// Runtime参照を持たずInspectorSnapshotだけをSection表示するRaven UI Viewです。
// Component変更はAction / Command層へ分離するため、このViewは現段階では読み取り専用です。
class InspectorRavenPanel
{
public:
    bool Attach(UIContext& context, const Ref<UIFontAtlas>& font);
    void Detach();
    void Update(const InspectorSnapshot& snapshot);
    void SetVisible(bool visible);

    bool IsAttached() const;
    bool IsVisible() const;

private:
    UICollapsibleSection* AddSection(
        UIElement& parent,
        const Ref<UIFontAtlas>& font,
        const std::string& title,
        std::size_t labelCount,
        std::vector<UILabel*>& labels);
    void UpdateLabels();

    UIContext* m_Context = nullptr;
    UIElement* m_Root = nullptr;
    UILabel* m_StatusLabel = nullptr;
    UICollapsibleSection* m_TagSection = nullptr;
    UICollapsibleSection* m_TransformSection = nullptr;
    UICollapsibleSection* m_CameraSection = nullptr;
    UICollapsibleSection* m_RigidBodySection = nullptr;
    UICollapsibleSection* m_ColliderSection = nullptr;
    std::vector<UILabel*> m_TagLabels;
    std::vector<UILabel*> m_TransformLabels;
    std::vector<UILabel*> m_CameraLabels;
    std::vector<UILabel*> m_RigidBodyLabels;
    std::vector<UILabel*> m_ColliderLabels;
    InspectorSnapshot m_Snapshot;
};

} // namespace Raven
