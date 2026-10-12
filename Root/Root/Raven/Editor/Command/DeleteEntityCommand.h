#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"

#include <optional>

namespace Raven
{

// Entity削除とComponent復元を1操作として扱うCommandです。
// Undoでは破棄済みHandleを復活させずScene::CreateEntity()から新しいGeneration付きHandleを取得し、
// 削除時に保存したComponentをその新Entityへ復元します。これにより、削除前Handleを保持する
// EditorやRuntimeの非所有参照が、Undo後のEntityを誤って有効と判断することを防ぎます。
class DeleteEntityCommand final : public IEditorCommand
{
public:
    explicit DeleteEntityCommand(Entity entity);

    bool CanExecute(const Scene& scene) const override;
    bool Execute(Scene& scene) override;
    bool Undo(Scene& scene) override;
    bool Redo(Scene& scene) override;

    // Undo後のEntityは新しいHandleを持つため、必要な利用側だけが明示的に取得します。
    Entity GetRestoredEntity() const;

private:
    bool DeleteCurrentEntity(Scene& scene);
    bool RestoreEntity(Scene& scene);

    Scene* m_TargetScene = nullptr;
    EntityHandle m_CurrentHandle = InvalidEntityHandle;
    bool m_Deleted = false;

    std::optional<TagComponent> m_Tag;
    std::optional<TransformComponent> m_Transform;
    std::optional<CameraComponent> m_Camera;
    std::optional<MeshRendererComponent> m_MeshRenderer;
    std::optional<MeshDeformationComponent> m_MeshDeformation;
    std::optional<AnimatorComponent> m_Animator;
    std::optional<RigidBodyComponent> m_RigidBody;
    std::optional<ColliderComponent> m_Collider;
};

} // namespace Raven
