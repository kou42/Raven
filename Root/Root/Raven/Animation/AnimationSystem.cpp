#include "Raven/Animation/AnimationSystem.h"

#include "Raven/Animation/Animator.h"
#include "Raven/Animation/AnimatorStateMachine.h"
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

        // StateMachineが設定されているEntityでは、Parameter/Transition評価とAnimator更新を
        // StateMachine側へ一本化します。ここでさらにAnimator::Update()を呼ぶと時間が
        // 1Frameに2回進むため、必ずどちらか片方だけを更新します。
        // StateMachineを持たない既存Entityは従来通りAnimator単体で再生できます。
        if (animatorComponent.StateMachine)
        {
            animatorComponent.StateMachine->Update(deltaTime);
        }
        else
        {
            animator.Update(deltaTime);
        }

        const TransformPose& pose = animator.GetCurrentPose();

        transform.Position = pose.Position;

        // Animation内部ではQuaternion + Slerpを正規表現にします。
        // Scene/Renderer側のTransformComponentは現時点でEuler角を保持しているため、
        // ECS境界でのみEuler XYZへ変換します。将来Transform自体をQuaternion化すれば
        // この変換は不要になり、AnimationからRendererまでQuaternionを維持できます。
        transform.Rotation = pose.Rotation.ToEulerXYZ();

        transform.Scale = pose.Scale;
    }
}

} // namespace Raven
