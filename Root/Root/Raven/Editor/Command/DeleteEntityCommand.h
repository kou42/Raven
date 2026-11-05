#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Scene.h"

#include <optional>
#include <string>
#include <vector>

namespace Raven
{

// Entityの削除と復元を1つの履歴操作として扱います。
// Undoで生成されるEntityは新しいHandleを持ち、破棄済みHandleは復活させません。
// 現段階では標準Componentを対象とし、未知のComponentを持つEntityは削除しません。
class DeleteEntityCommand final : public IEditorCommand
{
public:
    DeleteEntityCommand(Scene* targetScene, Entity target);

    void RemapEntityHandle(const EntityHandle& before, const EntityHandle& after) override;
    bool GetRestoredEntityHandles(EntityHandle& before, EntityHandle& after) const override;

    bool CanExecute(const Scene& scene) const override;

    bool Execute(Scene& scene) override;

    bool Undo(Scene& scene) override;

    bool Redo(Scene& scene) override;

private:
    template<class T>
    static std::optional<T> Capture(const Entity& entity);

    template<class T>
    static void Restore(Entity& entity, const std::optional<T>& value);

    Scene* m_TargetScene = nullptr;
    EntityHandle m_Handle{};
    EntityHandle m_PreviousHandle{};
    std::optional<TagComponent> m_Tag;
    std::optional<TransformComponent> m_Transform;
    std::optional<LocalTransformComponent> m_Local;
    std::optional<CameraComponent> m_Camera;
    std::optional<MeshRendererComponent> m_Mesh;
    std::optional<MeshDeformationComponent> m_Deformation;
    std::optional<AnimatorComponent> m_Animator;
    std::optional<RigidBodyComponent> m_Body;
    std::optional<ColliderComponent> m_Collider;
    std::optional<RelationshipComponent> m_Relationship;
    std::vector<EntityHandle> m_Children;
    std::vector<std::optional<LocalTransformComponent>> m_ChildrenLocal;
};

} // namespace Raven

#include "Raven/Editor/Command/DeleteEntityCommand.inl"
