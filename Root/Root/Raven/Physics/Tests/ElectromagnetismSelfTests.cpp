#include "Raven/Physics/Tests/ElectromagnetismSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Electromagnetism/ElectricCharge.h"
#include "Raven/Physics/Electromagnetism/ElectricField.h"
#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{
bool NearlyEqual(float left, float right, float epsilon = 1.0e-4f)
{
    return std::abs(left - right) <= epsilon;
}

Entity CreateChargedSphere(Scene& scene, const char* name, const math::Vec3& position, double charge)
{
    Entity entity = scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().Position = position;

    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(1.0f);
    rigidBody.UseGravity = false;
    rigidBody.AllowSleep = false;
    entity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 0.1f;
    entity.AddComponent<ColliderComponent>(collider);

    ElectricChargeComponent electricCharge{};
    electricCharge.ChargeCoulombs = charge;
    entity.AddComponent<ElectricChargeComponent>(electricCharge);
    return entity;
}
}

void RunElectromagnetismSelfTests()
{
    constexpr double microCoulomb = 1.0e-6;

    // 一様電場は評価位置に依存せず、設定したEをそのまま返します。
    const UniformElectricField uniformField({ 2.0f, -3.0f, 4.0f });
    const math::Vec3 uniformAtOrigin = uniformField.Evaluate({ 0.0f, 0.0f, 0.0f });
    const math::Vec3 uniformFarAway = uniformField.Evaluate({ 100.0f, -50.0f, 25.0f });
    assert(NearlyEqual(uniformAtOrigin.x, 2.0f));
    assert(NearlyEqual(uniformAtOrigin.y, -3.0f));
    assert(NearlyEqual(uniformAtOrigin.z, 4.0f));
    assert(NearlyEqual(uniformFarAway.x, uniformAtOrigin.x));
    assert(NearlyEqual(uniformFarAway.y, uniformAtOrigin.y));
    assert(NearlyEqual(uniformFarAway.z, uniformAtOrigin.z));

    // F=qE: 正電荷はEと同方向、負電荷は反対方向へ力を受けます。
    const math::Vec3 positiveElectricForce = ComputeElectricForce(2.0, { 3.0f, 0.0f, 0.0f });
    const math::Vec3 negativeElectricForce = ComputeElectricForce(-2.0, { 3.0f, 0.0f, 0.0f });
    assert(NearlyEqual(positiveElectricForce.x, 6.0f));
    assert(NearlyEqual(negativeElectricForce.x, -6.0f));

    // 点電荷電場: 正のsourceから+X側を評価すると電場は外向きの+Xになります。
    const PointChargeElectricField pointField(
        { 0.0f, 0.0f, 0.0f },
        microCoulomb);
    const math::Vec3 electricFieldAtOneMeter = pointField.Evaluate({ 1.0f, 0.0f, 0.0f });
    const math::Vec3 electricFieldAtTwoMeters = pointField.Evaluate({ 2.0f, 0.0f, 0.0f });
    assert(electricFieldAtOneMeter.x > 0.0f);
    assert(NearlyEqual(electricFieldAtOneMeter.y, 0.0f));
    assert(NearlyEqual(electricFieldAtOneMeter.z, 0.0f));
    assert(NearlyEqual(
        electricFieldAtTwoMeters.x / electricFieldAtOneMeter.x,
        0.25f,
        1.0e-3f));

    // 同符号電荷: x=0 のsourceから x=1 のtargetへ、+X方向の斥力が働きます。
    const math::Vec3 repulsiveForce = ComputeCoulombForce(
        { 0.0f, 0.0f, 0.0f },
        microCoulomb,
        { 1.0f, 0.0f, 0.0f },
        microCoulomb);
    assert(repulsiveForce.x > 0.0f);
    assert(NearlyEqual(repulsiveForce.y, 0.0f));
    assert(NearlyEqual(repulsiveForce.z, 0.0f));

    // Coulomb ForceはPointChargeElectricField + F=qEと同じ結果になることを確認します。
    const math::Vec3 forceFromField = ComputeElectricForce(microCoulomb, electricFieldAtOneMeter);
    assert(NearlyEqual(forceFromField.x, repulsiveForce.x, 1.0e-5f));
    assert(NearlyEqual(forceFromField.y, repulsiveForce.y, 1.0e-5f));
    assert(NearlyEqual(forceFromField.z, repulsiveForce.z, 1.0e-5f));

    // 異符号電荷: target側の力向きがsourceへ反転し、引力になります。
    const math::Vec3 attractiveForce = ComputeCoulombForce(
        { 0.0f, 0.0f, 0.0f },
        microCoulomb,
        { 1.0f, 0.0f, 0.0f },
        -microCoulomb);
    assert(attractiveForce.x < 0.0f);

    // 逆二乗則: 距離を2倍にすると力の大きさは1/4になります。
    const math::Vec3 forceAtTwoMeters = ComputeCoulombForce(
        { 0.0f, 0.0f, 0.0f },
        microCoulomb,
        { 2.0f, 0.0f, 0.0f },
        microCoulomb);
    assert(NearlyEqual(forceAtTwoMeters.x / repulsiveForce.x, 0.25f, 1.0e-3f));

    // Scene Systemでは同一ペアを1度だけ評価し、作用反作用を同じForceから加えます。
    Scene scene;
    Entity a = CreateChargedSphere(scene, "Positive Charge A", { -0.5f, 0.0f, 0.0f }, microCoulomb);
    Entity b = CreateChargedSphere(scene, "Positive Charge B", { 0.5f, 0.0f, 0.0f }, microCoulomb);

    ElectromagneticSystem system;
    system.ApplyCoulombForces(scene);

    const math::Vec3 forceA = a.GetComponent<RigidBodyComponent>().Force;
    const math::Vec3 forceB = b.GetComponent<RigidBodyComponent>().Force;
    assert(forceA.x < 0.0f);
    assert(forceB.x > 0.0f);
    assert(NearlyEqual(forceA.x + forceB.x, 0.0f, 1.0e-5f));
    assert(NearlyEqual(forceA.y + forceB.y, 0.0f, 1.0e-5f));
    assert(NearlyEqual(forceA.z + forceB.z, 0.0f, 1.0e-5f));

    // PhysicsSimulationWorldへの統合確認:
    // 同符号電荷を1 fixed-step進めると、Coulomb ForceがRigidBodyの通常Force経路で積分され、
    // 左側Bodyは-X、右側Bodyは+Xの速度を得ます。PhysicsWorld::ClearForces()後なので
    // accumulatorがゼロへ戻ることも合わせて確認し、step間の二重加算を防ぎます。
    Scene integratedScene;
    Entity integratedA = CreateChargedSphere(
        integratedScene,
        "Integrated Charge A",
        { -0.5f, 0.0f, 0.0f },
        microCoulomb);
    Entity integratedB = CreateChargedSphere(
        integratedScene,
        "Integrated Charge B",
        { 0.5f, 0.0f, 0.0f },
        microCoulomb);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    integratedScene.GetPhysicsSimulationWorld().StepSimulation(integratedScene, fixedDeltaTime);

    const RigidBodyComponent& integratedBodyA =
        integratedA.GetComponent<RigidBodyComponent>();
    const RigidBodyComponent& integratedBodyB =
        integratedB.GetComponent<RigidBodyComponent>();

    assert(integratedBodyA.LinearVelocity.x < 0.0f);
    assert(integratedBodyB.LinearVelocity.x > 0.0f);
    assert(NearlyEqual(
        integratedBodyA.LinearVelocity.x + integratedBodyB.LinearVelocity.x,
        0.0f,
        1.0e-5f));
    assert(integratedBodyA.Force.LengthSq() <= 1.0e-12f);
    assert(integratedBodyB.Force.LengthSq() <= 1.0e-12f);
}

} // namespace Raven::ph::tests
