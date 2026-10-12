#include "Raven/Editor/Command/DeleteEntityCommand.h"

#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace
{

template<class T>
std::optional<T> CaptureComponent(Entity entity)
{
    if (entity.HasComponent<T>() == false)
    {
        return std::nullopt;
    }
    return entity.GetComponent<T>();
}

template<class T>
void RestoreOptionalComponent(Entity entity, const std::optional<T>& component)
{
    if (component.has_value() == false)
    {
        return;
    }

    if (entity.HasComponent<T>() == true)
    {
        entity.GetComponent<T>() = *component;
        return;
    }
    entity.AddComponent<T>(*component);
}

} // namespace

DeleteEntityCommand::DeleteEntityCommand(Entity entity)
    : m_TargetScene(entity.GetScene())
    , m_CurrentHandle(entity.GetHandle())
{
    if (m_TargetScene == nullptr || m_TargetScene->IsEntityAlive(entity) == false)
    {
        return;
    }

    // ComponentStorageは型消去されているため、Editorが復元を保証する正規Component型を
    // ここで明示します。新しいScene Componentを追加した際にこの一覧も更新する必要があります。
    m_Tag = CaptureComponent<TagComponent>(entity);
    m_Transform = CaptureComponent<TransformComponent>(entity);
    m_Camera = CaptureComponent<CameraComponent>(entity);
    m_MeshRenderer = CaptureComponent<MeshRendererComponent>(entity);
    m_MeshDeformation = CaptureComponent<MeshDeformationComponent>(entity);
    m_Animator = CaptureComponent<AnimatorComponent>(entity);
    m_RigidBody = CaptureComponent<RigidBodyComponent>(entity);
    m_Collider = CaptureComponent<ColliderComponent>(entity);
}

bool DeleteEntityCommand::CanExecute(const Scene& scene) const
{
    return m_TargetScene != nullptr
        && m_TargetScene == &scene
        && m_Deleted == false
        && m_CurrentHandle.IsValid() == true
        && scene.IsEntityAlive(m_CurrentHandle) == true;
}

bool DeleteEntityCommand::Execute(Scene& scene)
{
    if (CanExecute(scene) == false)
    {
        return false;
    }
    return DeleteCurrentEntity(scene);
}

bool DeleteEntityCommand::Undo(Scene& scene)
{
    if (m_TargetScene == nullptr
        || m_TargetScene != &scene
        || m_Deleted == false
        || scene.IsEntityAlive(m_CurrentHandle) == true)
    {
        return false;
    }
    return RestoreEntity(scene);
}

bool DeleteEntityCommand::Redo(Scene& scene)
{
    if (m_TargetScene == nullptr
        || m_TargetScene != &scene
        || m_Deleted == true
        || scene.IsEntityAlive(m_CurrentHandle) == false)
    {
        return false;
    }
    return DeleteCurrentEntity(scene);
}

Entity DeleteEntityCommand::GetRestoredEntity() const
{
    if (m_TargetScene == nullptr
        || m_Deleted == true
        || m_TargetScene->IsEntityAlive(m_CurrentHandle) == false)
    {
        return Entity{};
    }
    return Entity(m_CurrentHandle, m_TargetScene);
}

bool DeleteEntityCommand::DeleteCurrentEntity(Scene& scene)
{
    scene.DestroyEntity(Entity(m_CurrentHandle, &scene));
    m_Deleted = scene.IsEntityAlive(m_CurrentHandle) == false;
    return m_Deleted;
}

bool DeleteEntityCommand::RestoreEntity(Scene& scene)
{
    const std::string name = m_Tag.has_value() == true
        ? m_Tag->Tag
        : "Entity";
    Entity restored = scene.CreateEntity(name);
    if (static_cast<bool>(restored) == false || scene.IsEntityAlive(restored) == false)
    {
        return false;
    }

    // CreateEntity()が標準で付与するTag / TransformもSnapshot値で上書きし、
    // それ以外のComponentは存在していた型だけを復元します。
    RestoreOptionalComponent(restored, m_Tag);
    RestoreOptionalComponent(restored, m_Transform);
    RestoreOptionalComponent(restored, m_Camera);
    RestoreOptionalComponent(restored, m_MeshRenderer);
    RestoreOptionalComponent(restored, m_MeshDeformation);
    RestoreOptionalComponent(restored, m_Animator);
    RestoreOptionalComponent(restored, m_RigidBody);
    RestoreOptionalComponent(restored, m_Collider);

    m_CurrentHandle = restored.GetHandle();
    m_Deleted = false;
    return true;
}

} // namespace Raven
