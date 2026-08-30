#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Entity.h"

#include <string>

namespace Raven
{

// Entityの表示名であるTagComponent::Tagを変更するCommandです。
// TransformCommandと同じくGeneration込みHandleで対象を識別し、Index再利用時の誤適用を防ぎます。
class RenameEntityCommand final : public IEditorCommand
{
public:
    RenameEntityCommand(Entity entity, std::string before, std::string after);

    bool CanExecute(const Scene& scene) const override;
    bool Execute(Scene& scene) override;
    bool Undo(Scene& scene) override;
    bool Redo(Scene& scene) override;

private:
    bool TryApply(Scene& scene, const std::string& name) const;

    EntityHandle m_Handle{};

    // 非所有Pointerです。現在のActive Sceneとの同一性確認だけに使い、dereferenceしません。
    Scene* m_TargetScene = nullptr;
    std::string m_Before;
    std::string m_After;
};

} // namespace Raven
