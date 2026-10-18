#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Entity.h"

#include <utility>
#include <vector>

namespace Raven
{

// Scene内のPrimary Cameraは排他的な状態なので、対象Cameraのboolだけではなく
// 編集前の全Camera状態を保存し、UndoでScene全体の選択状態を正確に復元します。
class SetPrimaryCameraCommand final : public IEditorCommand
{
public:
    SetPrimaryCameraCommand(Scene* targetScene, EntityHandle targetHandle);

    bool CanExecute(const Scene& scene) const override;
    bool Execute(Scene& scene) override;
    bool Undo(Scene& scene) override;
    bool Redo(Scene& scene) override;
    void RemapEntityHandle(
        const EntityHandle& before,
        const EntityHandle& after) override;

private:
    bool ApplyTarget(Scene& scene);
    bool RestoreBefore(Scene& scene) const;

    Scene* m_TargetScene = nullptr;
    EntityHandle m_TargetHandle = InvalidEntityHandle;
    std::vector<std::pair<EntityHandle, bool>> m_BeforeStates;
    bool m_CapturedBefore = false;
};

} // namespace Raven
