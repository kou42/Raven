#include "Raven/Physics/Tests/ElectromagnetismSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Electromagnetism/ElectricCharge.h"
#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"
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

    // 同符号電荷: x=0 のsourceから x=1 のtargetへ、+X方向の斥力が働きます。
    const math::Vec3 repulsiveForce = ComputeCoulombForce(
        { 0.0f, 0.0f, 0.0f },
        microCoulomb,
        { 1.0f, 0.0f, 0.0f },
        microCoulomb);
    assert(repulsiveForce.x > 0.0f);
    assert(NearlyEqual(repulsiveForce.y, 0.0f));
    assert(NearlyEqual(repulsiveForce.z, 0.0f));

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
}

} // namespace Raven::ph::tests
