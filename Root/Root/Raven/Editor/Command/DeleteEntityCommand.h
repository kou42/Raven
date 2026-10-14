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
    DeleteEntityCommand(Scene* targetScene, Entity target)
        : m_TargetScene(targetScene), m_Handle(target.GetHandle())
    {
    }

    bool CanExecute(const Scene& scene) const override
    {
        return m_TargetScene == &scene
            && m_Handle.IsValid() == true
            && scene.IsEntityAlive(m_Handle) == true
            && scene.CanSnapshotEntityComponents(m_Handle) == true
            && scene.HasComponent<TagComponent>(m_Handle.m_Index) == true;
    }

    bool Execute(Scene& scene) override
    {
        if (CanExecute(scene) == false)
        {
            return false;
        }

        const Entity entity(m_Handle, &scene);
        m_Tag = Capture<TagComponent>(entity);
        m_Transform = Capture<TransformComponent>(entity);
        m_Camera = Capture<CameraComponent>(entity);
        m_Mesh = Capture<MeshRendererComponent>(entity);
        m_Deformation = Capture<MeshDeformationComponent>(entity);
        m_Animator = Capture<AnimatorComponent>(entity);
        m_Body = Capture<RigidBodyComponent>(entity);
        m_Collider = Capture<ColliderComponent>(entity);
        m_Relationship = Capture<RelationshipComponent>(entity);
        // DestroyEntityが子をRootへ切り離す前に、直下の子を記録します。
        m_Children.clear();
        for (auto [child, relation] : scene.View<RelationshipComponent>())
        {
            if (child.GetHandle() != m_Handle && relation.Parent == m_Handle)
            {
                m_Children.push_back(child.GetHandle());
            }
        }

        scene.DestroyEntity(entity);
        return scene.IsEntityAlive(m_Handle) == false;
    }

    bool Undo(Scene& scene) override
    {
        if (m_TargetScene != &scene || m_Tag.has_value() == false
            || scene.IsEntityAlive(m_Handle) == true)
        {
            return false;
        }

        Entity restored = scene.CreateEntity(m_Tag->Tag);
        if (static_cast<bool>(restored) == false)
        {
            return false;
        }

        // CreateEntityが標準Tag/Transformを生成する場合は上書きし、
        // それ以外は追加します。外部の古いHandleを再利用しないことが重要です。
        try
        {
            Restore(restored, m_Tag);
            Restore(restored, m_Transform);
            Restore(restored, m_Camera);
            Restore(restored, m_Mesh);
            Restore(restored, m_Deformation);
            Restore(restored, m_Animator);
            Restore(restored, m_Body);
            Restore(restored, m_Collider);
            Restore(restored, m_Relationship);
            // 元のEntityにTransformが無かった場合はCreateEntityの既定値を残しません。
            if (m_Transform.has_value() == false)
            {
                restored.RemoveComponent<TransformComponent>();
            }
        }
        catch (...)
        {
            // Component追加途中の失敗で不完全なEntityをSceneに残さないよう巻き戻します。
            scene.DestroyEntity(restored);
            return false;
        }
        // 親が失われていた場合は古いGenerationへの参照を残しません。
        if (m_Relationship.has_value() == true &&
            m_Relationship->Parent.IsValid() == true &&
            scene.CanSetEntityParent(restored.GetHandle(), m_Relationship->Parent) == false)
        {
            restored.GetComponent<RelationshipComponent>().Parent = InvalidEntityHandle;
        }
        // 復元した親は新しいHandleなので、存続中の子だけを再接続します。
        for (EntityHandle child : m_Children)
        {
            if (scene.IsEntityAlive(child) == true &&
                scene.GetEntityParent(child).IsValid() == false &&
                scene.CanSetEntityParent(child, restored.GetHandle()) == true)
            {
                scene.SetEntityParent(child, restored.GetHandle());
            }
        }
        m_Handle = restored.GetHandle();
        return true;
    }

    bool Redo(Scene& scene) override
    {
        if (m_TargetScene != &scene || scene.IsEntityAlive(m_Handle) == false)
        {
            return false;
        }
        scene.DestroyEntity(Entity(m_Handle, &scene));
        return scene.IsEntityAlive(m_Handle) == false;
    }

private:
    template<class T>
    static std::optional<T> Capture(const Entity& entity)
    {
        if (entity.HasComponent<T>() == false)
        {
            return std::nullopt;
        }
        return entity.GetComponent<T>();
    }

    template<class T>
    static void Restore(Entity& entity, const std::optional<T>& value)
    {
        if (value.has_value() == false)
        {
            return;
        }
        if (entity.HasComponent<T>() == true)
        {
            entity.GetComponent<T>() = *value;
        }
        else
        {
            entity.AddComponent<T>(*value);
        }
    }

    Scene* m_TargetScene = nullptr;
    EntityHandle m_Handle{};
    std::optional<TagComponent> m_Tag;
    std::optional<TransformComponent> m_Transform;
    std::optional<CameraComponent> m_Camera;
    std::optional<MeshRendererComponent> m_Mesh;
    std::optional<MeshDeformationComponent> m_Deformation;
    std::optional<AnimatorComponent> m_Animator;
    std::optional<RigidBodyComponent> m_Body;
    std::optional<ColliderComponent> m_Collider;
    std::optional<RelationshipComponent> m_Relationship;
    std::vector<EntityHandle> m_Children;
};

} // namespace Raven
