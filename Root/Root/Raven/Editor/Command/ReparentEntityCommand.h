#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Scene.h"
#include <optional>

namespace Raven
{
// Sceneが親子関係の正規データを所有します。Local authoringの前後もSnapshotとして保存します。
class ReparentEntityCommand final : public IEditorCommand
{
public:
    ReparentEntityCommand(Scene* scene, EntityHandle child, EntityHandle parent,
        ReparentTransformMode mode = ReparentTransformMode::KeepWorld);
    void RemapEntityHandle(const EntityHandle& before, const EntityHandle& after) override;
    bool CanExecute(const Scene& scene) const override;
    bool Execute(Scene& scene) override;
    bool Undo(Scene& scene) override;
    bool Redo(Scene& scene) override;
private:
    Scene* m_Scene = nullptr;
    EntityHandle m_Child = InvalidEntityHandle;
    EntityHandle m_Before = InvalidEntityHandle;
    EntityHandle m_After = InvalidEntityHandle;
    ReparentTransformMode m_Mode = ReparentTransformMode::KeepWorld;
    std::optional<TransformComponent> m_BeforeLocal;
    std::optional<TransformComponent> m_AfterLocal;
    bool m_Executed = false;
};
}
