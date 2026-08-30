#include "Raven/Editor/Command/CreateEntityCommand.h"

#include "Raven/Scene/Scene.h"

#include <utility>

namespace Raven
{

CreateEntityCommand::CreateEntityCommand(Scene* targetScene, std::string name)
    : m_TargetScene(targetScene)
    , m_Name(std::move(name))
{
}

bool CreateEntityCommand::CanExecute(const Scene& scene) const
{
    if (m_TargetScene == nullptr || m_TargetScene != &scene)
    {
        return false;
    }

    // 初回ExecuteではHandleが無効です。Undo後はGenerationが進んだ古いHandleになるため、
    // どちらも「現在このCommandが生成したEntityは存在しない」という実行可能状態です。
    if (m_CreatedHandle.IsValid() == true
        && scene.IsEntityAlive(m_CreatedHandle) == true)
    {
        return false;
    }

    return true;
}

bool CreateEntityCommand::Execute(Scene& scene)
{
    if (CanExecute(scene) == false)
    {
        return false;
    }

    const Entity createdEntity = scene.CreateEntity(m_Name);
    if (static_cast<bool>(createdEntity) == false
        || scene.IsEntityAlive(createdEntity) == false)
    {
        return false;
    }

    m_CreatedHandle = createdEntity.GetHandle();
    return true;
}

bool CreateEntityCommand::Undo(Scene& scene)
{
    if (m_TargetScene == nullptr
        || m_TargetScene != &scene
        || m_CreatedHandle.IsValid() == false
        || scene.IsEntityAlive(m_CreatedHandle) == false)
    {
        return false;
    }

    scene.DestroyEntity(Entity(m_CreatedHandle, &scene));
    return scene.IsEntityAlive(m_CreatedHandle) == false;
}

bool CreateEntityCommand::Redo(Scene& scene)
{
    // UndoでGenerationが進んだため、Execute()がScene::CreateEntity()から新Handleを取得し直します。
    return Execute(scene);
}

Entity CreateEntityCommand::GetCreatedEntity() const
{
    if (m_TargetScene == nullptr || m_CreatedHandle.IsValid() == false)
    {
        return Entity{};
    }

    return Entity(m_CreatedHandle, m_TargetScene);
}

} // namespace Raven
