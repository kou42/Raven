#include "SceneGame.h"

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Animation/Animator.h"
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
    // Animation playback validation entity
    // ========================================================================
    // このEntityは AnimationClip -> Animator -> AnimatorComponent ->
    // AnimationSystem -> TransformComponent という一連の再生経路を目視確認するための
    // 最小サンプルです。
    //
    // あえてRigidBodyComponentを付けません。
    // Dynamic RigidBodyを持つEntityではPhysicsWorldがTransformの正規所有者になるため、
    // AnimationSystemと同じTransformを書き換えると競合するからです。
    Entity animatedCube = CreateEntity("AnimationTestCube");

    auto& transform = animatedCube.GetComponent<TransformComponent>();
    transform.Position = { -18.0f, 6.0f, -4.0f };
    transform.Rotation = { 0.0f, 0.0f, 0.0f };
    transform.Scale = { 4.0f, 4.0f, 4.0f };

    animatedCube.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_BoxMesh, m_Material });

    // ========================================================================
    // Clip data
    // ========================================================================
    // 4秒のLoop Clipを作り、Position / Rotation / Scaleを同時に変化させます。
    // Position/ScaleはVec3 Lerp、RotationはQuaternion Slerpで評価されます。
    auto clip = std::make_shared<AnimationClip>(4.0f);
    auto& track = clip->GetTransformTrack();

    track.PositionKeys =
    {
        { 0.0f, { -18.0f,  6.0f, -4.0f } },
        { 1.0f, { -18.0f, 14.0f, -4.0f } },
        { 2.0f, { -10.0f, 14.0f, -4.0f } },
        { 3.0f, { -10.0f,  6.0f, -4.0f } },
        { 4.0f, { -18.0f,  6.0f, -4.0f } }
    };

    // 0 -> 90 -> 180 -> 270 -> 360度とKeyを分割します。
    // Quaternionでは0度と360度が同じ姿勢を表すため、0度と360度の2Keyだけでは
    // Slerpが「回転しない最短経路」を選ぶ可能性があります。
    // 中間Keyを置くことで1周する意図を明示します。
    track.RotationKeys =
    {
        { 0.0f, math::Quat::FromEulerXYZ(0.0f, 0.0f, 0.0f) },
        { 1.0f, math::Quat::FromEulerXYZ(0.0f, math::Pi * 0.5f, 0.0f) },
        { 2.0f, math::Quat::FromEulerXYZ(0.0f, math::Pi,        0.0f) },
        { 3.0f, math::Quat::FromEulerXYZ(0.0f, math::Pi * 1.5f, 0.0f) },
        { 4.0f, math::Quat::FromEulerXYZ(0.0f, math::Pi * 2.0f, 0.0f) }
    };

    track.ScaleKeys =
    {
        { 0.0f, { 4.0f, 4.0f, 4.0f } },
        { 2.0f, { 7.0f, 7.0f, 7.0f } },
        { 4.0f, { 4.0f, 4.0f, 4.0f } }
    };

    // ========================================================================
    // Runtime playback state
    // ========================================================================
    // Clipは共有可能なデータ、AnimatorはEntityごとの再生状態です。
    // 同じclipを別Animatorへ渡せば、同じAnimationを異なる再生時刻/速度で共有できます。
    auto animator = std::make_shared<Animator>();
    animator->SetLoop(true);
    animator->SetSpeed(1.0f);
    animator->Play(clip);

    animatedCube.AddComponent<AnimatorComponent>(
        AnimatorComponent{ std::move(animator), true });

    m_AnimationTestEntity = animatedCube;
    m_SpawnedEntities.push_back(animatedCube);
}

} // namespace Raven
