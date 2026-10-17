#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Editor/Panels/InspectorSnapshot.h"

#include <functional>
#include <string>
#include <vector>

namespace Raven
{

class UIContext;
class UIElement;
class UIFontAtlas;
class UILabel;
class UICollapsibleSection;
class UIInputNumber;

// Runtime参照を持たずInspectorSnapshotだけを表示・編集するRaven UI Viewです。
// 編集要求はEntityHandle付きActionとしてEditorLayerへ返し、Scene / ComponentのLifetime検証と
// Undo Command生成をPanelの外へ分離します。
class InspectorRavenPanel
{
public:
    using TransformPreviewRequestedHandler =
        std::function<bool(EntityHandle, const InspectorTransformSnapshot&)>;
    using TransformCommitRequestedHandler =
        std::function<bool(EntityHandle, const InspectorTransformSnapshot&)>;
    using CameraPreviewRequestedHandler =
        std::function<bool(EntityHandle, const InspectorCameraSnapshot&)>;
    using CameraCommitRequestedHandler =
        std::function<bool(EntityHandle, const InspectorCameraSnapshot&)>;

    bool Attach(UIContext& context, const Ref<UIFontAtlas>& font);
    void Detach();
    void Update(const InspectorSnapshot& snapshot);
    void SetVisible(bool visible);
    void SetTransformPreviewRequestedHandler(TransformPreviewRequestedHandler handler);
    void SetTransformCommitRequestedHandler(TransformCommitRequestedHandler handler);
    void SetCameraPreviewRequestedHandler(CameraPreviewRequestedHandler handler);
    void SetCameraCommitRequestedHandler(CameraCommitRequestedHandler handler);

    bool IsAttached() const;
    bool IsVisible() const;

private:
    UICollapsibleSection* AddSection(
        UIElement& parent,
        const Ref<UIFontAtlas>& font,
        const std::string& title,
        std::size_t labelCount,
        std::vector<UILabel*>& labels);
    UICollapsibleSection* AddTransformSection(
        UIElement& parent,
        const Ref<UIFontAtlas>& font);
    void AddTransformInput(
        UIElement& parent,
        const Ref<UIFontAtlas>& font,
        const std::string& label,
        std::size_t valueIndex,
        double step);
    void SynchronizeTransformInputs(bool targetChanged);
    void PreviewTransformValue(std::size_t valueIndex, double value);
    void CommitTransformEdit();
    void ResetTransformEdit();
    UICollapsibleSection* AddCameraSection(
        UIElement& parent,
        const Ref<UIFontAtlas>& font);
    void AddCameraInput(
        UIElement& parent,
        const Ref<UIFontAtlas>& font,
        const std::string& label,
        std::size_t valueIndex,
        double step,
        double minimum,
        double maximum);
    void SynchronizeCameraInputs(bool targetChanged);
    void PreviewCameraValue(std::size_t valueIndex, double value);
    void CommitCameraEdit();
    void ResetCameraEdit();
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
    std::vector<UIInputNumber*> m_TransformInputs;
    std::vector<UIInputNumber*> m_CameraInputs;
    std::vector<UILabel*> m_CameraLabels;
    std::vector<UILabel*> m_RigidBodyLabels;
    std::vector<UILabel*> m_ColliderLabels;
    InspectorSnapshot m_Snapshot;
    TransformPreviewRequestedHandler m_OnTransformPreviewRequested;
    TransformCommitRequestedHandler m_OnTransformCommitRequested;
    CameraPreviewRequestedHandler m_OnCameraPreviewRequested;
    CameraCommitRequestedHandler m_OnCameraCommitRequested;
    EntityHandle m_TransformEditHandle = InvalidEntityHandle;
    InspectorTransformSnapshot m_TransformEditBefore{};
    bool m_TransformEditActive = false;
    bool m_SynchronizingTransformInputs = false;
    EntityHandle m_CameraEditHandle = InvalidEntityHandle;
    InspectorCameraSnapshot m_CameraEditBefore{};
    bool m_CameraEditActive = false;
    bool m_SynchronizingCameraInputs = false;
};

} // namespace Raven
