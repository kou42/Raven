#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Entity.h"

#include <string>

namespace Raven
{

// Scene::CreateEntity()によるEntity生成をUndo / Redo対象にするCommandです。
// Undo後のRedoでは破棄前Handleを復活させず、Sceneから新しいGeneration付きHandleを取得します。
// これにより、破棄済みHandleを再び有効化して外部の古い参照と衝突させることを防ぎます。
class CreateEntityCommand final : public IEditorCommand
{
public:
    CreateEntityCommand(Scene* targetScene, std::string name);

    bool CanExecute(const Scene& scene) const override;
    bool Execute(Scene& scene) override;
    bool Undo(Scene& scene) override;
    bool Redo(Scene& scene) override;

    // Execute直後にHierarchyが新規Entityを選択するためのSnapshotです。
    // CommandやSceneの所有権は渡さず、現在Handleから一時的なEntity値を構築します。
    Entity GetCreatedEntity() const;

private:
    Scene* m_TargetScene = nullptr;
    std::string m_Name;
    EntityHandle m_CreatedHandle{};
};

} // namespace Raven
