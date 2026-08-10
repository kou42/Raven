#include "Raven/Animation/AnimationSystem.h"

#include "Raven/Animation/Animator.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

void AnimationSystem::Update(Scene& scene, float deltaTime)
{
    // ========================================================================
    // ECS -> Animator -> Transform bridge
    // ========================================================================
    // AnimatorComponentとTransformComponentを同時に持つEntityだけを走査します。
    // Animator自身はScene/ECSを知らず、System側がPoseをTransformへ適用することで
    // Animation runtimeとSceneの依存方向を一方向に保ちます。
    for (auto [entity, animatorComponent, transform]
        : scene.View<AnimatorComponent, TransformComponent>())
    {
        if (!animatorComponent.Enabled || !animatorComponent.IsValid())
        {
            continue;
        }

        // ====================================================================
        // Animation / Physics Transform ownership
        // ====================================================================
        // Dynamic BodyはPhysicsWorldがTransformの正規所有者です。
        // ここでAnimationがTransformを書き換えると、同一Frame内でPhysics結果と競合し、
        // jitterやteleportの原因になります。
        //
        // Static / Kinematicはゲーム側からTransformを与える用途があるためAnimationを許可します。
        // Skeletal Animationを導入した後は、Character root motionとPhysicsの関係を別途設計します。
        if (const auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex()))
        {
            if (rigidBody->Type == BodyType::Dynamic)
            {
                continue;
            }
        }

        Animator& animator = *animatorComponent.Instance;
        animator.Update(deltaTime);

        const TransformPose& pose = animator.GetCurrentPose();

        transform.Position = pose.Position;
        transform.Rotation = pose.Rotation;
        transform.Scale = pose.Scale;
    }
}

} // namespace Raven
