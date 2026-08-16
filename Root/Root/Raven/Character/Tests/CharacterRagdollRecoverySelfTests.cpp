// Raven/Character/Tests/CharacterRagdollRecoverySelfTests.cpp
#include <cassert>
#include <cmath>

#include "Raven/Character/CharacterController.h"

namespace Raven::tests
{
namespace
{

bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-5f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

void RunCharacterRagdollRecoverySelfTests()
{
    CharacterControllerConfig config{};
    config.GroundHeight = 1.25f;

    CharacterController controller(config);
    TransformComponent transform{};
    transform.Position = { -10.0f, 8.0f, 3.0f };
    transform.Rotation = { 0.7f, -2.0f, -0.4f };

    // ========================================================================
    // Grounded Ragdoll Recovery
    // ========================================================================
    // Ragdollから復帰するときはController Rootを指定World位置へ移し、倒れ姿勢のPitch / Rollを
    // Character Transformへ残さずYawだけを採用します。またGrounded状態へ戻す場合、最後の
    // 下向きPhysics速度は0へClampされ、水平速度だけが次のKinematic Updateへ継承されます。
    const math::Vec3 recoveryPosition{ 4.0f, config.GroundHeight, -7.0f };
    const math::Vec3 inheritedVelocity{ 2.0f, -5.0f, -1.5f };
    constexpr float recoveryYaw = 0.75f;

    assert(controller.RestoreAfterRagdoll(
        recoveryPosition,
        recoveryYaw,
        inheritedVelocity,
        true,
        transform));

    assert(NearlyEqual(transform.Position.x, recoveryPosition.x));
    assert(NearlyEqual(transform.Position.y, recoveryPosition.y));
    assert(NearlyEqual(transform.Position.z, recoveryPosition.z));
    assert(NearlyEqual(transform.Rotation.x, 0.0f));
    assert(NearlyEqual(transform.Rotation.y, recoveryYaw));
    assert(NearlyEqual(transform.Rotation.z, 0.0f));

    assert(controller.IsGrounded() == true);
    assert(NearlyEqual(controller.GetVelocity().x, inheritedVelocity.x));
    assert(NearlyEqual(controller.GetVelocity().y, 0.0f));
    assert(NearlyEqual(controller.GetVelocity().z, inheritedVelocity.z));

    // ========================================================================
    // Airborne Recovery State
    // ========================================================================
    // SnapControllerToGround=false相当の経路では、呼び出し側が空中復帰を選択できます。
    // その場合は鉛直速度も維持し、次のCharacterController::Update()でGravity処理へ接続します。
    const math::Vec3 airbornePosition{ 1.0f, 5.0f, 2.0f };
    const math::Vec3 airborneVelocity{ -1.0f, 3.0f, 0.5f };

    assert(controller.RestoreAfterRagdoll(
        airbornePosition,
        -0.5f,
        airborneVelocity,
        false,
        transform));

    assert(controller.IsGrounded() == false);
    assert(NearlyEqual(controller.GetVelocity().x, airborneVelocity.x));
    assert(NearlyEqual(controller.GetVelocity().y, airborneVelocity.y));
    assert(NearlyEqual(controller.GetVelocity().z, airborneVelocity.z));
}

} // namespace Raven::tests
