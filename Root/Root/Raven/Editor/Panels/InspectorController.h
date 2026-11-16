#pragma once

#include "Raven/Editor/Panels/InspectorSnapshot.h"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace Raven
{
class Scene;
class InspectorRavenPanel;

// 現在の選択を値で取得します。M3は単一選択だけを編集し、Count>1は拒否します。
struct InspectorSelection
{
    Entity Primary;
    std::size_t Count = 0u;
};

// Modelの生存確認と既存Undo Command操作を担当します。Scene/Selectionは所有しません。
// Viewより長生きさせ、破棄前にはUnbindを呼びます。Providerの捕捉先もControllerより長生きさせます。
class InspectorController
{
public:
    InspectorController();
    InspectorController(const InspectorController&) = delete;
    InspectorController& operator=(const InspectorController&) = delete;
    InspectorController(InspectorController&&) = delete;
    InspectorController& operator=(InspectorController&&) = delete;
    using SceneProvider = std::function<Scene*()>;
    using SelectionProvider = std::function<InspectorSelection()>;
    void Bind(InspectorRavenPanel& panel, SceneProvider scene, SelectionProvider selection);
    void Unbind(InspectorRavenPanel& panel);
    void ResetPresentation();
    InspectorSnapshot PullSnapshot();
    bool PreviewTransform(EntityHandle handle, const InspectorTransformSnapshot& requested);
    bool CommitTransform(EntityHandle handle, const InspectorTransformSnapshot& beforeSnapshot);
    bool PreviewCamera(EntityHandle handle, const InspectorCameraSnapshot& requested);
    bool CommitCamera(EntityHandle handle, const InspectorCameraSnapshot& before);
    bool SetPrimaryCamera(EntityHandle handle);
    bool PreviewRigidBody(EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& requested);
    bool CommitRigidBody(EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& before);
    bool ChangeRigidBody(EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& requested);
    bool PreviewCollider(EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& requested);
    bool CommitCollider(EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& before);
    bool ChangeCollider(EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& requested);
private:
    Scene* ResolveTarget(EntityHandle handle) const;
    SceneProvider m_SceneProvider;
    SelectionProvider m_SelectionProvider;
    // 保存するScene pointerは比較専用。逆参照は必ずProviderから再取得した生存中のSceneで行います。
    Scene* m_PresentedScene = nullptr;
    EntityHandle m_PresentedHandle = InvalidEntityHandle;
    std::size_t m_PresentedCount = 0u;
    std::uint64_t m_PresentedHistoryEpoch = 0u;
    std::uint64_t m_SelectionRevision = 0u;
    bool m_PresentationKnown = false;
    bool m_PresentationEditable = false;
};
} // namespace Raven
