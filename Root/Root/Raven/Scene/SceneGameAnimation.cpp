#include "SceneGame.h"

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Animation/Animator.h"
#include "Raven/Animation/AnimatorStateMachine.h"
#include "Raven/Scene/Components.h"

#include <algorithm>
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
    // このEntityは AnimationClip -> BlendTree1D / AnimatorStateMachine -> Animator ->
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
    // AnimationClip側も同じ縦横比を維持し、Motionごとの大きさだけを変化させます。
    transform.Scale = { 2.5f, 4.0f, 1.5f };

    animatedCube.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_BoxMesh, m_Material });

    // ========================================================================
    // Motion validation Clips
    // ========================================================================
    // 本物のCharacter Assetを必要とせずState遷移とBlend Tree補間を目視確認できるよう、
    // 各MotionをCubeの高さ・回転・Scaleの違いとして表現します。
    //
    // LocomotionではIdle / Walk / Runが別Stateではなく1D Blend TreeのChildになります。
    // Speed=0.0 -> 2.0 -> 6.0の間で回転とScaleが連続的に補間されることを確認できます。
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

    // JumpはJumpStart / Fall / Landへ分割し、State境界を目視しやすい形状差で表現します。
    // 実Character Asset導入前でも「離陸 -> 落下 -> 着地」が別Stateとして切り替わることを確認できます。
    auto jumpStartClip = makeClip(10.0f, 0.0f, 3.5f);
    auto fallClip = makeClip(13.0f, 0.0f, 4.0f);
    auto landClip = makeClip(5.5f, 0.0f, 5.0f);

    // ========================================================================
    // Runtime State Machine + 1D Blend Tree
    // ========================================================================
    // BuildCharacterBlendTreeController()でLocomotionを1 Stateへまとめます。
    // State MachineはLocomotion <-> Jump系だけを管理し、Idle / Walk / Runの連続補間は
    // BlendTree1DがSpeed Parameterから直接Poseを生成します。
    auto animator = std::make_shared<Animator>();
    animator->SetLoop(true);
    animator->SetSpeed(1.0f);

    auto stateMachine = std::make_shared<AnimatorStateMachine>(*animator);
    if (!stateMachine->BuildCharacterBlendTreeController(
            idleClip,
            walkClip,
            runClip,
            jumpStartClip,
            fallClip,
            landClip))
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
    // Deterministic Blend Tree + Jump validation sequence
    // ========================================================================
    // 8秒周期でSpeedを0 -> 2 -> 6へ連続変化させた後、JumpStart -> Fall -> Land ->
    // Locomotionへ戻る流れを自動再生します。
    //
    // 重要なのは0/2/6へ瞬時切替しないことです。
    // Threshold間を毎Frame通過させることで、Blend Weightが
    //   Idle 1.0 -> Idle/Walk混合 -> Walk 1.0
    //   Walk 1.0 -> Walk/Run混合 -> Run 1.0
    // と連続変化することを画面右上のAnimation Debug Overlayで確認できます。
    const float previousTime = m_AnimationStateMachineTime;
    m_AnimationStateMachineTime += deltaTime;
    if (m_AnimationStateMachineTime >= 8.0f)
    {
        m_AnimationStateMachineTime -= 8.0f;
    }

    const float time = m_AnimationStateMachineTime;
    float speed = 0.0f;
    bool grounded = true;
    float verticalVelocity = 0.0f;

    // ========================================================================
    // Smooth Speed: 0 -> 2 -> 6
    // ========================================================================
    // 0～2秒: Idle Threshold(0)からWalk Threshold(2)まで線形に増加。
    // 2～4秒: Walk Threshold(2)からRun Threshold(6)まで線形に増加。
    // 4～7秒: Run Threshold(6)を維持し、5秒地点からJump検証へ移ります。
    if (time < 2.0f)
    {
        const float t = std::clamp(time / 2.0f, 0.0f, 1.0f);
        speed = 2.0f * t;
    }
    else if (time < 4.0f)
    {
        const float t = std::clamp((time - 2.0f) / 2.0f, 0.0f, 1.0f);
        speed = 2.0f + (6.0f - 2.0f) * t;
    }
    else if (time < 7.0f)
    {
        speed = 6.0f;
    }

    // 5秒地点でJump要求を1回だけ発生させ、その後約1秒間を空中として扱います。
    // VerticalVelocityは簡易的な放物運動として、5.5秒で0を跨ぐように作ります。
    // これによりJumpStart -> FallがAnimation時間ではなく上下方向速度で発火することを確認できます。
    const bool crossedJumpPoint = previousTime < 5.0f && time >= 5.0f;
    if (time >= 5.0f && time < 6.0f)
    {
        grounded = false;
        verticalVelocity = (5.5f - time) * 12.0f;
    }

    // Jump要求を出すFrameだけは接地中として扱い、Jump Trigger && Groundedを成立させます。
    // 実GameplayではJump要求を受理した直後にPhysicsが上向き速度を与え、次Frame以降Grounded=falseになります。
    if (crossedJumpPoint)
    {
        grounded = true;
    }

    animatorComponent.StateMachine->UpdateCharacterParameters(
        speed,
        grounded,
        crossedJumpPoint,
        verticalVelocity);
}

} // namespace Raven
