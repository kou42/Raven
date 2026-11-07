#include "Raven/Editor/Command/ReparentEntityCommand.h"

namespace Raven
{
ReparentEntityCommand::ReparentEntityCommand(Scene* scene, EntityHandle child, EntityHandle parent, ReparentTransformMode mode)
    : m_Scene(scene), m_Child(child), m_After(parent), m_Mode(mode)
{
}
void ReparentEntityCommand::RemapEntityHandle(const EntityHandle& before, const EntityHandle& after)
{
    if (m_Child == before) { m_Child = after; }
    if (m_Before == before) { m_Before = after; }
    if (m_After == before) { m_After = after; }
}
bool ReparentEntityCommand::CanExecute(const Scene& scene) const
{
    return m_Scene == &scene && scene.CanSetEntityParent(m_Child, m_After, m_Mode) == true
        && scene.GetEntityParent(m_Child) != m_After;
}
bool ReparentEntityCommand::Execute(Scene& scene)
{
    if (CanExecute(scene) == false)
    {
        return false;
    }
    m_Before = scene.GetEntityParent(m_Child);
    m_BeforeLocal.reset();
    m_AfterLocal.reset();
    if (scene.HasComponent<LocalTransformComponent>(m_Child.m_Index) == true)
    {
        math::Mat4 world;
        if (scene.TryGetEntityWorldMatrix(m_Child, world) == false)
        {
            return false;
        }
        m_BeforeLocal = scene.GetComponent<LocalTransformComponent>(m_Child.m_Index).Value;
    }
    m_Executed = scene.SetEntityParent(m_Child, m_After, m_Mode);
    if (m_Executed == true && m_BeforeLocal.has_value() == true)
    {
        m_AfterLocal = scene.GetComponent<LocalTransformComponent>(m_Child.m_Index).Value;
    }
    return m_Executed;
}
bool ReparentEntityCommand::Undo(Scene& scene)
{
    if (m_Executed == false || m_Scene != &scene || scene.IsEntityAlive(m_Child) == false
        || scene.HasComponent<LocalTransformComponent>(m_Child.m_Index) != m_BeforeLocal.has_value())
    {
        return false;
    }
    return m_BeforeLocal.has_value() == true
        ? scene.SetEntityParentAndLocal(m_Child, m_Before, *m_BeforeLocal)
        : scene.SetEntityParent(m_Child, m_Before, m_Mode);
}
bool ReparentEntityCommand::Redo(Scene& scene)
{
    if (m_Executed == false || m_Scene != &scene || scene.IsEntityAlive(m_Child) == false
        || scene.HasComponent<LocalTransformComponent>(m_Child.m_Index) != m_AfterLocal.has_value())
    {
        return false;
    }
    return m_AfterLocal.has_value() == true
        ? scene.SetEntityParentAndLocal(m_Child, m_After, *m_AfterLocal)
        : scene.SetEntityParent(m_Child, m_After, m_Mode);
}
}
