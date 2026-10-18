#include "Raven/Editor/Command/SetPrimaryCameraCommand.h"

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"

namespace Raven
{

SetPrimaryCameraCommand::SetPrimaryCameraCommand(
    Scene* targetScene,
    EntityHandle targetHandle)
    : m_TargetScene(targetScene)
    , m_TargetHandle(targetHandle)
{
}

bool SetPrimaryCameraCommand::CanExecute(const Scene& scene) const
{
    if (m_TargetScene == nullptr || m_TargetScene != &scene
        || m_TargetHandle.IsValid() == false
        || scene.IsEntityAlive(m_TargetHandle) == false)
    {
        return false;
    }

    const Entity target(m_TargetHandle, const_cast<Scene*>(&scene));
    return target.HasComponent<TransformComponent>() == true
        && target.HasComponent<CameraComponent>() == true
        && target.GetComponent<CameraComponent>().Primary == false;
}

bool SetPrimaryCameraCommand::Execute(Scene& scene)
{
    if (CanExecute(scene) == false)
    {
        return false;
    }

    if (m_CapturedBefore == false)
    {
        // 複数Primaryを含む不正な既存SceneでもUndoを可逆にするため、
        // 正規化前の全Cameraフラグをそのまま保存します。
        m_BeforeStates.clear();
        for (auto [entity, camera] : scene.View<CameraComponent>())
        {
            m_BeforeStates.emplace_back(entity.GetHandle(), camera.Primary);
        }
        m_CapturedBefore = true;
    }
    return ApplyTarget(scene);
}

bool SetPrimaryCameraCommand::Undo(Scene& scene)
{
    return RestoreBefore(scene);
}

bool SetPrimaryCameraCommand::Redo(Scene& scene)
{
    return ApplyTarget(scene);
}

void SetPrimaryCameraCommand::RemapEntityHandle(
    const EntityHandle& before,
    const EntityHandle& after)
{
    if (m_TargetHandle == before)
    {
        m_TargetHandle = after;
    }
    for (auto& state : m_BeforeStates)
    {
        if (state.first == before)
        {
            state.first = after;
        }
    }
}

bool SetPrimaryCameraCommand::ApplyTarget(Scene& scene)
{
    if (m_TargetScene != &scene || scene.IsEntityAlive(m_TargetHandle) == false)
    {
        return false;
    }
    return SceneCameraSystem::SetPrimaryCamera(
        scene, Entity(m_TargetHandle, &scene));
}

bool SetPrimaryCameraCommand::RestoreBefore(Scene& scene) const
{
    if (m_TargetScene != &scene || m_CapturedBefore == false)
    {
        return false;
    }

    for (auto [entity, camera] : scene.View<CameraComponent>())
    {
        static_cast<void>(entity);
        camera.Primary = false;
    }

    // Cameraが削除された場合は存続中のHandleだけを復元します。Indexが再利用されても
    // Generationが異なるHandleへ古いPrimary状態を誤適用しません。
    for (const auto& state : m_BeforeStates)
    {
        if (scene.IsEntityAlive(state.first) == false)
        {
            continue;
        }
        Entity entity(state.first, &scene);
        if (entity.HasComponent<CameraComponent>() == true)
        {
            entity.GetComponent<CameraComponent>().Primary = state.second;
        }
    }
    return true;
}

} // namespace Raven
