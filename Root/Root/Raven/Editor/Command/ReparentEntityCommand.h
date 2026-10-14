#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

// Sceneが親子関係の正規データを所有し、Editor Commandは変更前後のHandleだけを保持します。
class ReparentEntityCommand final : public IEditorCommand
{
public:
    ReparentEntityCommand(Scene* scene, EntityHandle child, EntityHandle parent)
        : m_Scene(scene), m_Child(child), m_After(parent)
    {
    }

    void RemapEntityHandle(const EntityHandle& before, const EntityHandle& after) override
    {
        if (m_Child == before) { m_Child = after; }
        if (m_Before == before) { m_Before = after; }
        if (m_After == before) { m_After = after; }
    }

    bool CanExecute(const Scene& scene) const override
    {
        return m_Scene == &scene
            && scene.CanSetEntityParent(m_Child, m_After) == true
            && scene.GetEntityParent(m_Child) != m_After;
    }

    bool Execute(Scene& scene) override
    {
        if (CanExecute(scene) == false) { return false; }
        m_Before = scene.GetEntityParent(m_Child);
        m_Executed = scene.SetEntityParent(m_Child, m_After);
        return m_Executed;
    }

    bool Undo(Scene& scene) override
    {
        if (m_Executed == false || m_Scene != &scene ||
            scene.CanSetEntityParent(m_Child, m_Before) == false)
        {
            return false;
        }
        return scene.SetEntityParent(m_Child, m_Before);
    }

    bool Redo(Scene& scene) override
    {
        if (m_Executed == false || m_Scene != &scene ||
            scene.CanSetEntityParent(m_Child, m_After) == false)
        {
            return false;
        }
        return scene.SetEntityParent(m_Child, m_After);
    }

private:
    Scene* m_Scene = nullptr;
    EntityHandle m_Child = InvalidEntityHandle;
    EntityHandle m_Before = InvalidEntityHandle;
    EntityHandle m_After = InvalidEntityHandle;
    bool m_Executed = false;
};

} // namespace Raven
