#include "SceneGame.h"

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Animation/Animator.h"
#include "Raven/Animation/AnimatorStateMachine.h"
#include "Raven/Scene/Components.h"

#include <memory>

namespace Raven
{

void SceneGame::SpawnAnimationTestCube()
{
    if (!m_BoxMesh || !m_Material)
    {
        return;
    }

    // ========================================================================
    // Animation State Machine validation entity
    // ========================================================================
    // このEntityは AnimationClip -> AnimatorStateMachine -> Animator ->
    // AnimatorComponent -> AnimationSystem -> TransformComponent という一連の経路を
    // 目視確認するための最小Characterサンプルです。
    //
    // あえてRigidBodyComponentを付けません。
    // Dynamic RigidBodyを持つEntityではPhysicsWorldがTransformの正規所有者になるため、
    // AnimationSystemと同じTransformを書き換えると競合するからです。
    Entity animatedCube = CreateEntity("AnimationStateMachineTestCube");

    auto& transform = animatedCube.GetComponent<TransformComponent>();
    transform.Position = { -18.0f, 6.0f, -4.0f };
    transform.Rotation = { 0.0f, 0.0f, 0.0f };

    // 完全な立方体はY軸回転してもシルエットの変化が小さく、RotationKeyが正しく
    // 描画へ反映されているか目視しづらいため、X/Zを細くした直方体として表示します。
    // AnimationClip側も同じ縦横比を維持し、Stateごとの大きさだけを変化させます。
    transform.Scale = { 2.5f, 4.0f, 1.5f };

    animatedCube.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_BoxMesh, m_Material });

    // ========================================================================
    // State validation Clips
    // ========================================================================
    // 本物のCharacter Assetを必要とせずState遷移だけを目視確認できるよう、各Stateを
    // Cubeの高さ・回転・Scaleの違いとして表現します。各Clipは同じ基準位置を使うため、
    // CrossFade時にも意図しない大きな位置移動が発生しません。
    //
    // scaleはY方向の高さを基準値として受け取り、X/Zには異なる比率を掛けています。
    // これによりWalk/RunのY軸回転時に長辺・短辺の向きが明確に変わり、
    // RotationKey -> AnimationSystem -> Transformの動作を一目で確認できます。
    auto makeClip = [](float height, float yaw, float scale)
    {
        auto clip = std::make_shared<AnimationClip>(1.0f);
        auto& track = clip->GetTransformTrack();
        track.PositionKeys =
        {
            { 0.0f, { -18.0f, height, -4.0f } },
            { 1.0f, { -18.0f, height, -4.0f } }
        };
        track.RotationKeys =
        {
            { 0.0f, math::Quat::FromEulerXYZ(0.0f, 0.0f, 0.0f) },
            { 1.0f, math::Quat::FromEulerXYZ(0.0f, yaw, 0.0f) }
        };
        track.ScaleKeys =
        {
            { 0.0f, { scale * 0.625f, scale, scale * 0.375f } },
            { 1.0f, { scale * 0.625f, scale, scale * 0.375f } }
        };
        return clip;
    };

    auto idleClip = makeClip(6.0f, 0.0f, 4.0f);
    auto walkClip = makeClip(6.0f, math::Pi * 0.5f, 4.5f);
    auto runClip  = makeClip(6.0f, math::Pi, 5.0f);

    // Jumpだけ高さを上げ、Grounded / Jump Triggerによる遷移が目で分かるようにします。
    auto jumpClip = std::make_shared<AnimationClip>(1.0f);
    auto& jumpTrack = jumpClip->GetTransformTrack();
    jumpTrack.PositionKeys =
    {
        { 0.0f, { -18.0f,  6.0f, -4.0f } },
        { 0.5f, { -18.0f, 13.0f, -4.0f } },
        { 1.0f, { -18.0f,  6.0f, -4.0f } }
    };
    jumpTrack.RotationKeys =
    {
        { 0.0f, math::Quat::Identity() },
        { 1.0f, math::Quat::Identity() }
    };
    jumpTrack.ScaleKeys =
    {
        { 0.0f, { 4.5f * 0.625f, 4.5f, 4.5f * 0.375f } },
        { 1.0f, { 4.5f * 0.625f, 4.5f, 4.5f * 0.375f } }
    };

    // ========================================================================
    // Runtime State Machine
    // ========================================================================
    // BuildCharacterController()でState / Parameter / Transitionをまとめて構築します。
    // Scene側はTransition条件の詳細を知らず、毎Frame Character状態だけを渡します。
    auto animator = std::make_shared<Animator>();
    animator->SetLoop(true);
    animator->SetSpeed(1.0f);

    auto stateMachine = std::make_shared<AnimatorStateMachine>(*animator);
    if (!stateMachine->BuildCharacterController(idleClip, walkClip, runClip, jumpClip))
    {
        return;
    }

    AnimatorComponent animatorComponent{};
    animatorComponent.Instance = animator;
    animatorComponent.StateMachine = stateMachine;
    animatorComponent.Enabled = true;
    animatedCube.AddComponent<AnimatorComponent>(std::move(animatorComponent));

    m_AnimationStateMachineTime = 0.0f;
    m_AnimationTestEntity = animatedCube;
    m_SpawnedEntities.push_back(animatedCube);
}

void SceneGame::UpdateAnimationStateMachineTest(float deltaTime)
{
    if (!m_AnimationTestEntity || !m_AnimationTestEntity.HasComponent<AnimatorComponent>())
    {
        return;
    }

    auto& animatorComponent = m_AnimationTestEntity.GetComponent<AnimatorComponent>();
    if (!animatorComponent.StateMachine)
    {
        return;
    }

    // ========================================================================
    // Deterministic State Machine validation sequence
    // ========================================================================
    // 8秒周期で Idle -> Walk -> Run -> Jump -> Run -> Walk -> Idle を自動再生します。
    // 入力やPhysicsに依存しないため、Animation層の変更だけを切り分けて確認できます。
    // Jump TriggerはJump区間へ入る瞬間の1Frameだけ立てます。
    const float previousTime = m_AnimationStateMachineTime;
    m_AnimationStateMachineTime += deltaTime;
    if (m_AnimationStateMachineTime >= 8.0f)
    {
        m_AnimationStateMachineTime -= 8.0f;
    }

    const float time = m_AnimationStateMachineTime;
    float speed = 0.0f;
    bool grounded = true;

    if (time >= 2.0f && time < 4.0f)
    {
        speed = 2.0f; // Walk領域
    }
    else if (time >= 4.0f && time < 7.0f)
    {
        speed = 6.0f; // Run領域
    }

    // 5秒地点でJump要求を1回だけ発生させ、その後約1秒間を空中として扱います。
    const bool crossedJumpPoint = previousTime < 5.0f && time >= 5.0f;
    if (time >= 5.0f && time < 6.0f)
    {
        grounded = crossedJumpPoint;
    }

    animatorComponent.StateMachine->UpdateCharacterParameters(
        speed,
        grounded,
        crossedJumpPoint);
}

} // namespace Raven
