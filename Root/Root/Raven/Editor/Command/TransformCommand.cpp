#include "Raven/Editor/Command/TransformCommand.h"

#include "Raven/Scene/Scene.h"

#include <cmath>

namespace Raven
{
namespace
{

constexpr float TransformCompareEpsilon = 0.000001f;

bool NearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= TransformCompareEpsilon;
}

} // namespace

TransformCommand::TransformCommand(
    Entity entity,
    const TransformComponent& before,
    const TransformComponent& after)
    : m_Handle(entity.GetHandle())
    , m_TargetScene(entity.GetScene())
    , m_Before(before)
    , m_After(after)
{
}

bool TransformCommand::CanExecute(const Scene& scene) const
{
    // 過去SceneのPointerをdereferenceせず、現在Sceneとの同一性だけを確認します。
    if (m_TargetScene == nullptr || m_TargetScene != &scene || HasTransformChange() == false)
    {
        return false;
    }

    const Entity entity(m_Handle, const_cast<Scene*>(&scene));

    // IsEntityAliveはHandleのGenerationも検証します。
    // 対象EntityまたはTransformComponentが消えているCommandは安全に失敗させます。
    if (scene.IsEntityAlive(entity) == false
        || entity.HasComponent<TransformComponent>() == false)
    {
        return false;
    }

    return true;
}

bool TransformCommand::Execute(Scene& scene)
{
    return TryApply(scene, m_After);
}

bool TransformCommand::Undo(Scene& scene)
{
    return TryApply(scene, m_Before);
}

bool TransformCommand::Redo(Scene& scene)
{
    return TryApply(scene, m_After);
}

bool TransformCommand::TryApply(Scene& scene, const TransformComponent& transform) const
{
    if (CanExecute(scene) == false)
    {
        return false;
    }

    Entity entity(m_Handle, &scene);
    entity.GetComponent<TransformComponent>() = transform;
    return true;
}

bool TransformCommand::HasTransformChange() const
{
    const bool isEqual =
        NearlyEqual(m_Before.Position.x, m_After.Position.x)
        && NearlyEqual(m_Before.Position.y, m_After.Position.y)
        && NearlyEqual(m_Before.Position.z, m_After.Position.z)
        && NearlyEqual(m_Before.Rotation.x, m_After.Rotation.x)
        && NearlyEqual(m_Before.Rotation.y, m_After.Rotation.y)
        && NearlyEqual(m_Before.Rotation.z, m_After.Rotation.z)
        && NearlyEqual(m_Before.Scale.x, m_After.Scale.x)
        && NearlyEqual(m_Before.Scale.y, m_After.Scale.y)
        && NearlyEqual(m_Before.Scale.z, m_After.Scale.z);

    return isEqual == false;
}

} // namespace Raven
