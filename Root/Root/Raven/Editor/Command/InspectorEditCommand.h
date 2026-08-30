#pragma once

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

#include <utility>

namespace Raven
{

// Inspectorで編集する値と、その値をComponentへ安全に適用する処理を履歴化するCommandです。
// ApplyFunctionをcapture不可能な関数Pointerに限定することで、Panel内の一時変数やComponent参照を
// Commandの寿命まで誤って保持することを防ぎます。CameraやRigidBodyのように専用Setterが必要な型も、
// Undo / Redo時に同じApplyFunctionを通るためComponent固有の不変条件を維持できます。
template<class TValue>
class InspectorEditCommand final : public IEditorCommand
{
public:
    using ValidateFunction = bool (*)(Entity entity);
    using ApplyFunction = bool (*)(Entity entity, const TValue& value);
    using EqualsFunction = bool (*)(const TValue& a, const TValue& b);

    InspectorEditCommand(
        Entity entity,
        TValue before,
        TValue after,
        ValidateFunction validateFunction,
        ApplyFunction applyFunction,
        EqualsFunction equalsFunction)
        : m_Handle(entity.GetHandle())
        , m_TargetScene(entity.GetScene())
        , m_Before(std::move(before))
        , m_After(std::move(after))
        , m_ValidateFunction(validateFunction)
        , m_ApplyFunction(applyFunction)
        , m_EqualsFunction(equalsFunction)
    {
    }

    bool CanExecute(const Scene& scene) const override
    {
        if (m_TargetScene == nullptr
            || m_TargetScene != &scene
            || m_ValidateFunction == nullptr
            || m_ApplyFunction == nullptr
            || m_EqualsFunction == nullptr
            || m_EqualsFunction(m_Before, m_After) == true)
        {
            return false;
        }

        Entity entity(m_Handle, const_cast<Scene*>(&scene));
        if (scene.IsEntityAlive(entity) == false
            || m_ValidateFunction(entity) == false)
        {
            return false;
        }

        return true;
    }

    bool Execute(Scene& scene) override
    {
        return TryApply(scene, m_After);
    }

    bool Undo(Scene& scene) override
    {
        return TryApply(scene, m_Before);
    }

    bool Redo(Scene& scene) override
    {
        return TryApply(scene, m_After);
    }

private:
    bool TryApply(Scene& scene, const TValue& value) const
    {
        if (CanExecute(scene) == false)
        {
            return false;
        }

        Entity entity(m_Handle, &scene);
        return m_ApplyFunction(entity, value);
    }

    EntityHandle m_Handle{};
    Scene* m_TargetScene = nullptr;
    TValue m_Before{};
    TValue m_After{};
    ValidateFunction m_ValidateFunction = nullptr;
    ApplyFunction m_ApplyFunction = nullptr;
    EqualsFunction m_EqualsFunction = nullptr;
};

} // namespace Raven
