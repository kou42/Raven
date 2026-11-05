#include "Raven/Editor/Command/DeleteEntityCommand.h"

namespace Raven
{

DeleteEntityCommand::DeleteEntityCommand(Scene* targetScene, Entity target)
    : m_TargetScene(targetScene)
    , m_Handle(target.GetScene() == targetScene ? target.GetHandle() : InvalidEntityHandle)
{
}

void DeleteEntityCommand::RemapEntityHandle(const EntityHandle& before, const EntityHandle& after)
{
    for (EntityHandle& child : m_Children)
    {
        if (child == before) { child = after; }
    }
    if (m_Relationship.has_value() == true && m_Relationship->Parent == before)
    {
        m_Relationship->Parent = after;
    }
}

bool DeleteEntityCommand::GetRestoredEntityHandles(EntityHandle& before, EntityHandle& after) const
{
    if (m_PreviousHandle.IsValid() == false || m_PreviousHandle == m_Handle)
    {
        return false;
    }
    before = m_PreviousHandle;
    after = m_Handle;
    return true;
}

bool DeleteEntityCommand::CanExecute(const Scene& scene) const
{
    return m_TargetScene == &scene
        && m_Handle.IsValid() == true
        && scene.IsEntityAlive(m_Handle) == true
        && scene.CanSnapshotEntityComponents(m_Handle) == true
        && scene.HasComponent<TagComponent>(m_Handle.m_Index) == true;
}

bool DeleteEntityCommand::Execute(Scene& scene)
{
    if (CanExecute(scene) == false)
    {
        return false;
    }

    if (scene.HasComponent<LocalTransformComponent>(m_Handle.m_Index) == true)
    {
        math::Mat4 world;
        if (scene.TryGetEntityWorldMatrix(m_Handle, world) == false)
        {
            return false;
        }
    }
    const Entity entity(m_Handle, &scene);
    m_Tag = Capture<TagComponent>(entity);
    m_Transform = Capture<TransformComponent>(entity);
    m_Local = Capture<LocalTransformComponent>(entity);
    m_Camera = Capture<CameraComponent>(entity);
    m_Mesh = Capture<MeshRendererComponent>(entity);
    m_Deformation = Capture<MeshDeformationComponent>(entity);
    m_Animator = Capture<AnimatorComponent>(entity);
    m_Body = Capture<RigidBodyComponent>(entity);
    m_Collider = Capture<ColliderComponent>(entity);
    m_Relationship = Capture<RelationshipComponent>(entity);
    // DestroyEntityが子をRootへ切り離す前に、直下の子を記録します。
    m_Children = scene.GetEntityChildren(m_Handle);
    m_ChildrenLocal.clear();
    for (EntityHandle child : m_Children)
    {
        if (scene.HasComponent<LocalTransformComponent>(child.m_Index) == true)
        {
            math::Mat4 world;
            if (scene.TryGetEntityWorldMatrix(child, world) == false)
            {
                return false;
            }
        }
        m_ChildrenLocal.push_back(Capture<LocalTransformComponent>(Entity(child, &scene)));
    }

    scene.DestroyEntity(entity);
    return scene.IsEntityAlive(m_Handle) == false;
}

bool DeleteEntityCommand::Undo(Scene& scene)
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
        // Parentを直接復元すると古いHandleや循環を一時的に持つため、空の関係からAPIで復元します。
        if (m_Relationship.has_value() == true)
        {
            restored.AddComponent<RelationshipComponent>();
            if (scene.CanSetEntityParent(restored.GetHandle(), m_Relationship->Parent) == true)
            {
                scene.SetEntityParent(restored.GetHandle(), m_Relationship->Parent);
            }
        }
        if (m_Local.has_value() == true)
        {
            restored.AddComponent<LocalTransformComponent>(*m_Local);
            // 保存した親が消えていれば、削除前のWorldを新しいRootのLocalに引き継ぎます。
            const TransformComponent value = m_Relationship.has_value() == true
                && m_Relationship->Parent.IsValid() == true && scene.GetEntityParent(restored.GetHandle()).IsValid() == false
                ? *m_Transform : m_Local->Value;
            if (scene.SetLocalTransform(restored.GetHandle(), value) == false)
            {
                scene.DestroyEntity(restored);
                return false;
            }
        }
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
    // 親が失われていた場合は上の検証で再接続せず、Rootに留めます。
    // 復元した親は新しいHandleなので、存続中の子だけを再接続します。
    // 全候補の幾何検証を先に終え、1つ目の子だけ復元された状態で失敗しないようにします。
    for (std::size_t i = 0; i < m_Children.size(); ++i)
    {
        const EntityHandle child = m_Children[i];
        if (scene.IsEntityAlive(child) == true && scene.GetEntityParent(child).IsValid() == false)
        {
            const bool local = m_ChildrenLocal[i].has_value() == true && scene.HasComponent<LocalTransformComponent>(child.m_Index) == true;
            const bool valid = local == true
                ? scene.CanSetEntityParentAndLocal(child, restored.GetHandle(), m_ChildrenLocal[i]->Value)
                : scene.CanSetEntityParent(child, restored.GetHandle());
            if (valid == false)
            {
                scene.DestroyEntity(restored);
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < m_Children.size(); ++i)
    {
        const EntityHandle child = m_Children[i];
        if (scene.IsEntityAlive(child) == true && scene.GetEntityParent(child).IsValid() == false)
        {
            if (m_ChildrenLocal[i].has_value() == true && scene.HasComponent<LocalTransformComponent>(child.m_Index) == true)
            {
                scene.SetEntityParentAndLocal(child, restored.GetHandle(), m_ChildrenLocal[i]->Value);
            }
            else
            {
                scene.SetEntityParent(child, restored.GetHandle());
            }
        }
    }
    m_PreviousHandle = m_Handle;
    m_Handle = restored.GetHandle();
    return true;
}

bool DeleteEntityCommand::Redo(Scene& scene)
{
    if (m_TargetScene != &scene || scene.IsEntityAlive(m_Handle) == false)
    {
        return false;
    }
    scene.DestroyEntity(Entity(m_Handle, &scene));
    return scene.IsEntityAlive(m_Handle) == false;
}

}
