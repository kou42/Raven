#include "Raven/Physics/Tests/ElectromagnetismSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Electromagnetism/ElectricCharge.h"
#include "Raven/Physics/Electromagnetism/ElectricField.h"
#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"
#include "Raven/Physics/Electromagnetism/MagneticField.h"
#include "Raven/Physics/Field/GravityField.h"
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

    // 一様GravityFieldは位置に依存せず重力加速度を返します。
    const UniformGravityField gravityField({ 0.0f, -3.5f, 1.0f });
    const math::Vec3 gravityAtOrigin = gravityField.Evaluate({ 0.0f, 0.0f, 0.0f });
    const math::Vec3 gravityFarAway = gravityField.Evaluate({ 100.0f, 50.0f, -20.0f });
    assert(NearlyEqual(gravityAtOrigin.x, 0.0f));
    assert(NearlyEqual(gravityAtOrigin.y, -3.5f));
    assert(NearlyEqual(gravityAtOrigin.z, 1.0f));
    assert(NearlyEqual(gravityFarAway.y, gravityAtOrigin.y));

    // PointGravityFieldは中心へ向かう逆二乗則の加速度を返します。
    // 1mと2mで加速度比が1:1/4になること、中心を跨いだとき方向が反転することを確認します。
    const PointGravityField pointGravityField({ 0.0f, 0.0f, 0.0f }, 12.0f);
    const math::Vec3 pointGravityAtOneMeter = pointGravityField.Evaluate({ 1.0f, 0.0f, 0.0f });
    const math::Vec3 pointGravityAtTwoMeters = pointGravityField.Evaluate({ 2.0f, 0.0f, 0.0f });
    const math::Vec3 pointGravityAtNegativeX = pointGravityField.Evaluate({ -1.0f, 0.0f, 0.0f });
    assert(pointGravityAtOneMeter.x < 0.0f);
    assert(pointGravityAtNegativeX.x > 0.0f);
    assert(NearlyEqual(pointGravityAtOneMeter.x, -12.0f));
    assert(NearlyEqual(pointGravityAtTwoMeters.x / pointGravityAtOneMeter.x, 0.25f, 1.0e-3f));

    // 点質量中心では方向が定義できないため、SimulationへNaN/Infを流さずゼロを返します。
    const math::Vec3 pointGravityAtCenter = pointGravityField.Evaluate({ 0.0f, 0.0f, 0.0f });
    assert(pointGravityAtCenter.LengthSq() <= 1.0e-12f);

    // MinDistance内では距離をClampし、中心近傍でも有限な加速度を維持します。
    const PointGravityField softenedPointGravity({ 0.0f, 0.0f, 0.0f }, 10.0f, 0.5f);
    const math::Vec3 softenedGravity = softenedPointGravity.Evaluate({ 0.25f, 0.0f, 0.0f });
    assert(NearlyEqual(softenedGravity.x, -40.0f));

    // 既存SetGravity()/GetGravity()はFieldが所有する同じ値へ接続されます。
    Scene gravityScene;
    PhysicsWorld& gravityWorld = gravityScene.GetPhysicsSimulationWorld().GetRigidBodyWorld();
    gravityWorld.SetGravity({ 0.0f, -4.25f, 0.5f });
    const math::Vec3 legacyGravity = gravityWorld.GetGravity();
    const math::Vec3 fieldGravity = gravityWorld.GetGravityField().Evaluate({ 10.0f, 20.0f, 30.0f });
    assert(NearlyEqual(legacyGravity.x, fieldGravity.x));
    assert(NearlyEqual(legacyGravity.y, fieldGravity.y));
    assert(NearlyEqual(legacyGravity.z, fieldGravity.z));

    const UniformElectricField uniformField({ 2.0f, -3.0f, 4.0f });
    const math::Vec3 uniformAtOrigin = uniformField.Evaluate({ 0.0f, 0.0f, 0.0f });
    const math::Vec3 uniformFarAway = uniformField.Evaluate({ 100.0f, -50.0f, 25.0f });
    assert(NearlyEqual(uniformAtOrigin.x, 2.0f));
    assert(NearlyEqual(uniformAtOrigin.y, -3.0f));
    assert(NearlyEqual(uniformAtOrigin.z, 4.0f));
    assert(NearlyEqual(uniformFarAway.x, uniformAtOrigin.x));
    assert(NearlyEqual(uniformFarAway.y, uniformAtOrigin.y));
    assert(NearlyEqual(uniformFarAway.z, uniformAtOrigin.z));

    const math::Vec3 positiveElectricForce = ComputeElectricForce(2.0, { 3.0f, 0.0f, 0.0f });
    const math::Vec3 negativeElectricForce = ComputeElectricForce(-2.0, { 3.0f, 0.0f, 0.0f });
    assert(NearlyEqual(positiveElectricForce.x, 6.0f));
    assert(NearlyEqual(negativeElectricForce.x, -6.0f));

    // 一様磁場は位置に依存せずBを返し、Lorentz Forceはq(v x B)の向きになります。
    const UniformMagneticField uniformMagneticField({ 0.0f, 0.0f, 2.0f });
    const math::Vec3 magneticAtOrigin = uniformMagneticField.Evaluate({ 0.0f, 0.0f, 0.0f });
    const math::Vec3 magneticFarAway = uniformMagneticField.Evaluate({ 50.0f, -20.0f, 10.0f });
    assert(NearlyEqual(magneticAtOrigin.z, 2.0f));
    assert(NearlyEqual(magneticFarAway.z, magneticAtOrigin.z));

    const math::Vec3 positiveMagneticForce = ComputeMagneticForce(
        2.0, { 3.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 4.0f });
    const math::Vec3 negativeMagneticForce = ComputeMagneticForce(
        -2.0, { 3.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 4.0f });
    assert(NearlyEqual(positiveMagneticForce.x, 0.0f));
    assert(NearlyEqual(positiveMagneticForce.y, -24.0f));
    assert(NearlyEqual(positiveMagneticForce.z, 0.0f));
    assert(NearlyEqual(negativeMagneticForce.y, 24.0f));

    // vとBが平行なら外積はゼロとなり、磁気力もゼロです。
    const math::Vec3 parallelMagneticForce = ComputeMagneticForce(
        3.0, { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, 2.0f });
    assert(parallelMagneticForce.LengthSq() <= 1.0e-12f);

    const PointChargeElectricField pointField({ 0.0f, 0.0f, 0.0f }, microCoulomb);
    const math::Vec3 electricFieldAtOneMeter = pointField.Evaluate({ 1.0f, 0.0f, 0.0f });
    const math::Vec3 electricFieldAtTwoMeters = pointField.Evaluate({ 2.0f, 0.0f, 0.0f });
    assert(electricFieldAtOneMeter.x > 0.0f);
    assert(NearlyEqual(electricFieldAtTwoMeters.x / electricFieldAtOneMeter.x, 0.25f, 1.0e-3f));

    const math::Vec3 repulsiveForce = ComputeCoulombForce(
        { 0.0f, 0.0f, 0.0f }, microCoulomb,
        { 1.0f, 0.0f, 0.0f }, microCoulomb);
    assert(repulsiveForce.x > 0.0f);

    const math::Vec3 forceFromField = ComputeElectricForce(microCoulomb, electricFieldAtOneMeter);
    assert(NearlyEqual(forceFromField.x, repulsiveForce.x, 1.0e-5f));

    const math::Vec3 attractiveForce = ComputeCoulombForce(
        { 0.0f, 0.0f, 0.0f }, microCoulomb,
        { 1.0f, 0.0f, 0.0f }, -microCoulomb);
    assert(attractiveForce.x < 0.0f);

    const math::Vec3 forceAtTwoMeters = ComputeCoulombForce(
        { 0.0f, 0.0f, 0.0f }, microCoulomb,
        { 2.0f, 0.0f, 0.0f }, microCoulomb);
    assert(NearlyEqual(forceAtTwoMeters.x / repulsiveForce.x, 0.25f, 1.0e-3f));

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

    // 外部Field Registryは同一Fieldの二重登録を拒否し、複数Fieldを線形に重ね合わせます。
    UniformElectricField fieldX({ 10.0f, 0.0f, 0.0f });
    UniformElectricField fieldY({ 0.0f, 20.0f, 0.0f });
    ElectromagneticSystem externalFieldSystem;
    assert(externalFieldSystem.RegisterElectricField(fieldX) == true);
    assert(externalFieldSystem.RegisterElectricField(fieldX) == false);
    assert(externalFieldSystem.RegisterElectricField(fieldY) == true);
    assert(externalFieldSystem.GetRegisteredElectricFieldCount() == 2u);

    Scene externalFieldScene;
    Entity positiveCharge = CreateChargedSphere(
        externalFieldScene, "External Field Positive", { 0.0f, 0.0f, 0.0f }, 2.0);
    Entity negativeCharge = CreateChargedSphere(
        externalFieldScene, "External Field Negative", { 1.0f, 0.0f, 0.0f }, -3.0);
    externalFieldSystem.ApplyElectricFieldForces(externalFieldScene);

    const math::Vec3 positiveForce = positiveCharge.GetComponent<RigidBodyComponent>().Force;
    const math::Vec3 negativeForce = negativeCharge.GetComponent<RigidBodyComponent>().Force;
    assert(NearlyEqual(positiveForce.x, 20.0f));
    assert(NearlyEqual(positiveForce.y, 40.0f));
    assert(NearlyEqual(negativeForce.x, -30.0f));
    assert(NearlyEqual(negativeForce.y, -60.0f));

    assert(externalFieldSystem.UnregisterElectricField(fieldX) == true);
    assert(externalFieldSystem.UnregisterElectricField(fieldX) == false);
    assert(externalFieldSystem.ContainsElectricField(fieldY) == true);
    externalFieldSystem.ClearElectricFields();
    assert(externalFieldSystem.GetRegisteredElectricFieldCount() == 0u);

    // MagneticField RegistryもElectricFieldと同じ非所有・重ね合わせ契約を確認します。
    UniformMagneticField magneticZ({ 0.0f, 0.0f, 2.0f });
    UniformMagneticField magneticY({ 0.0f, 1.0f, 0.0f });
    ElectromagneticSystem magneticSystem;
    assert(magneticSystem.RegisterMagneticField(magneticZ) == true);
    assert(magneticSystem.RegisterMagneticField(magneticZ) == false);
    assert(magneticSystem.RegisterMagneticField(magneticY) == true);
    assert(magneticSystem.GetRegisteredMagneticFieldCount() == 2u);

    Scene magneticScene;
    Entity magneticCharge = CreateChargedSphere(
        magneticScene, "Magnetic Charge", { 0.0f, 0.0f, 0.0f }, 2.0);
    magneticCharge.GetComponent<RigidBodyComponent>().LinearVelocity = { 3.0f, 0.0f, 0.0f };
    magneticSystem.ApplyMagneticFieldForces(magneticScene);

    // v=(3,0,0), B=(0,1,2)なので v x B=(0,-6,3)、q=2よりF=(0,-12,6)です。
    const math::Vec3 magneticForce = magneticCharge.GetComponent<RigidBodyComponent>().Force;
    assert(NearlyEqual(magneticForce.x, 0.0f));
    assert(NearlyEqual(magneticForce.y, -12.0f));
    assert(NearlyEqual(magneticForce.z, 6.0f));

    assert(magneticSystem.UnregisterMagneticField(magneticZ) == true);
    assert(magneticSystem.UnregisterMagneticField(magneticZ) == false);
    assert(magneticSystem.ContainsMagneticField(magneticY) == true);
    magneticSystem.ClearMagneticFields();
    assert(magneticSystem.GetRegisteredMagneticFieldCount() == 0u);

    Scene integratedScene;
    Entity integratedA = CreateChargedSphere(
        integratedScene, "Integrated Charge A", { -0.5f, 0.0f, 0.0f }, microCoulomb);
    Entity integratedB = CreateChargedSphere(
        integratedScene, "Integrated Charge B", { 0.5f, 0.0f, 0.0f }, microCoulomb);

    constexpr float fixedDeltaTime = 1.0f / 60.0f;
    integratedScene.GetPhysicsSimulationWorld().StepSimulation(integratedScene, fixedDeltaTime);

    const RigidBodyComponent& integratedBodyA = integratedA.GetComponent<RigidBodyComponent>();
    const RigidBodyComponent& integratedBodyB = integratedB.GetComponent<RigidBodyComponent>();
    assert(integratedBodyA.LinearVelocity.x < 0.0f);
    assert(integratedBodyB.LinearVelocity.x > 0.0f);
    assert(NearlyEqual(integratedBodyA.LinearVelocity.x + integratedBodyB.LinearVelocity.x, 0.0f, 1.0e-5f));
    assert(integratedBodyA.Force.LengthSq() <= 1.0e-12f);
    assert(integratedBodyB.Force.LengthSq() <= 1.0e-12f);
}

} // namespace Raven::ph::tests
