#include "Raven/Editor/Command/RenameEntityCommand.h"

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <utility>

namespace Raven
{

RenameEntityCommand::RenameEntityCommand(
    Entity entity,
    std::string before,
    std::string after)
    : m_Handle(entity.GetHandle())
    , m_TargetScene(entity.GetScene())
    , m_Before(std::move(before))
    , m_After(std::move(after))
{
}

bool RenameEntityCommand::CanExecute(const Scene& scene) const
{
    // 名前が変化していない編集や別Scene用Commandは履歴へ登録しません。
    if (m_TargetScene == nullptr
        || m_TargetScene != &scene
        || m_Before == m_After)
    {
        return false;
    }

    const Entity entity(m_Handle, const_cast<Scene*>(&scene));

    // Entity削除後やTagComponent削除後のUndo / Redoは安全に失敗させます。
    // IsEntityAlive()がGenerationまで検証するため、同じIndexの新Entityへ旧名を適用しません。
    if (scene.IsEntityAlive(entity) == false
        || entity.HasComponent<TagComponent>() == false)
    {
        return false;
    }

    return true;
}

bool RenameEntityCommand::Execute(Scene& scene)
{
    return TryApply(scene, m_After);
}

bool RenameEntityCommand::Undo(Scene& scene)
{
    return TryApply(scene, m_Before);
}

bool RenameEntityCommand::Redo(Scene& scene)
{
    return TryApply(scene, m_After);
}

bool RenameEntityCommand::TryApply(Scene& scene, const std::string& name) const
{
    if (CanExecute(scene) == false)
    {
        return false;
    }

    Entity entity(m_Handle, &scene);
    entity.GetComponent<TagComponent>().Tag = name;
    return true;
}

} // namespace Raven
