#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

// EntityHandleのIndexとGenerationを保持するため、破棄済みEntityと同じIndexが再利用されても
// 別Entityへ古いTransformを誤適用しません。
class TransformCommand final : public IEditorCommand
{
public:
    TransformCommand(Entity entity, const TransformComponent& before, const TransformComponent& after);

    bool CanExecute(const Scene& scene) const override;
    bool Execute(Scene& scene) override;
    bool Undo(Scene& scene) override;
    bool Redo(Scene& scene) override;

private:
    bool TryApply(Scene& scene, const TransformComponent& transform) const;
    bool HasTransformChange() const;

    EntityHandle m_Handle{};

    // 非所有Pointerです。dereferenceせず、Active Sceneとの同一性確認だけに使います。
    Scene* m_TargetScene = nullptr;
    TransformComponent m_Before{};
    TransformComponent m_After{};
};

} // namespace Raven
