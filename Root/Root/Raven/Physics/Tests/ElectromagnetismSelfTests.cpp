#include "Raven/Physics/Tests/ElectromagnetismSelfTests.h"

#include <cassert>
#include <cmath>

#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Electromagnetism/BarnesHutCoulombSolver.h"
#include "Raven/Physics/Electromagnetism/ElectricCharge.h"
#include "Raven/Physics/Electromagnetism/ElectricField.h"
#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"
#include "Raven/Physics/Electromagnetism/MagneticField.h"
#include "Raven/Physics/Electromagnetism/Spatial/CoulombOctree.h"
#include "Raven/Physics/Astro/CelestialBody.h"
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

Entity CreateGravityTestSphere(
    Scene& scene,
    const char* name,
    const math::Vec3& position,
    bool useGravity)
{
    Entity entity = scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().Position = position;

    RigidBodyComponent rigidBody{};
    rigidBody.SetBodyType(BodyType::Dynamic);
    rigidBody.SetMass(1.0f);
    rigidBody.UseGravity = useGravity;
    rigidBody.AllowSleep = false;
    rigidBody.LinearDamping = 0.0f;
    rigidBody.AngularDamping = 0.0f;
    entity.AddComponent<RigidBodyComponent>(rigidBody);

    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 0.1f;
    entity.AddComponent<ColliderComponent>(collider);
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
    assert(NearlyEqual(gravityFarAway.x, gravityAtOrigin.x));
    assert(NearlyEqual(gravityFarAway.y, gravityAtOrigin.y));
    assert(NearlyEqual(gravityFarAway.z, gravityAtOrigin.z));

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
    const math::Vec3 defaultGravity = gravityWorld.GetGravity();
    assert(NearlyEqual(defaultGravity.x, 0.0f));
    assert(NearlyEqual(defaultGravity.y, -9.80665f));
    assert(NearlyEqual(defaultGravity.z, 0.0f));

    gravityWorld.SetGravity({ 0.0f, -4.0f, 0.0f });
    const math::Vec3 legacyGravity = gravityWorld.GetGravity();
    const math::Vec3 fieldGravity = gravityWorld.GetGravityField().Evaluate({ 10.0f, 20.0f, 30.0f });
    assert(NearlyEqual(legacyGravity.x, fieldGravity.x));
    assert(NearlyEqual(legacyGravity.y, fieldGravity.y));
    assert(NearlyEqual(legacyGravity.z, fieldGravity.z));

    // RigidBodyはFieldが返した加速度だけをfixed-stepで速度へ積分し、UseGravityを尊重します。
    // Dampingを0に固定し、1 step後の差分を重力加速度 * dtとして直接検証します。
    Entity gravityEnabledBody = CreateGravityTestSphere(
        gravityScene, "Gravity Enabled", { -10.0f, 0.0f, 0.0f }, true);
    Entity gravityDisabledBody = CreateGravityTestSphere(
        gravityScene, "Gravity Disabled", { 10.0f, 0.0f, 0.0f }, false);
    constexpr float gravityFixedDeltaTime = 1.0f / 60.0f;
    gravityWorld.Step(gravityScene, gravityFixedDeltaTime);

    const math::Vec3 enabledVelocity =
        gravityEnabledBody.GetComponent<RigidBodyComponent>().LinearVelocity;
    const math::Vec3 disabledVelocity =
        gravityDisabledBody.GetComponent<RigidBodyComponent>().LinearVelocity;
    assert(NearlyEqual(enabledVelocity.x, 0.0f));
    assert(NearlyEqual(enabledVelocity.y, -4.0f * gravityFixedDeltaTime));
    assert(NearlyEqual(enabledVelocity.z, 0.0f));
    assert(NearlyEqual(disabledVelocity.x, 0.0f));
    assert(NearlyEqual(disabledVelocity.y, 0.0f));
    assert(NearlyEqual(disabledVelocity.z, 0.0f));

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

    const PointChargeElectricField pointField({ 0.0f, 0.0f, 0.0f }, microCoulomb);
    const math::Vec3 electricFieldAtOneMeter = pointField.Evaluate({ 1.0f, 0.0f, 0.0f });
    const math::Vec3 electricFieldAtTwoMeters = pointField.Evaluate({ 2.0f, 0.0f, 0.0f });
    assert(electricFieldAtOneMeter.x > 0.0f);
    assert(NearlyEqual(electricFieldAtTwoMeters.x / electricFieldAtOneMeter.x, 0.25f, 1.0e-3f));

    // Coulomb Octreeは正負電荷を相殺せず別々に集約します。
    // 総電荷が0になるdipoleでも両極性の電荷量と中心を保持し、将来の近似評価で情報を失いません。
    std::vector<CoulombOctreeBody> coulombTreeBodies{
        { { -2.0, 0.0, 0.0 }, 2.0 },
        { { 2.0, 0.0, 0.0 }, -2.0 },
        { { 4.0, 0.0, 0.0 }, 1.0 }
    };
    CoulombOctree coulombOctree;
    coulombOctree.Build(coulombTreeBodies);
    assert(coulombOctree.GetRootIndex() >= 0);
    const CoulombOctreeNode& coulombRoot =
        coulombOctree.GetNodes()[static_cast<std::size_t>(coulombOctree.GetRootIndex())];
    assert(std::abs(coulombRoot.PositiveCharge - 3.0) < 1.0e-12);
    assert(std::abs(coulombRoot.NegativeChargeMagnitude - 2.0) < 1.0e-12);
    assert(std::abs(coulombRoot.PositiveCenter[0]) < 1.0e-12);
    assert(std::abs(coulombRoot.NegativeCenter[0] - 2.0) < 1.0e-12);

    // 完全な正負相殺でも各極性のaggregateは消えません。
    std::vector<CoulombOctreeBody> neutralTreeBodies{
        { { -1.0, 0.0, 0.0 }, 1.0 },
        { { 1.0, 0.0, 0.0 }, -1.0 }
    };
    coulombOctree.Build(neutralTreeBodies);
    const CoulombOctreeNode& neutralRoot =
        coulombOctree.GetNodes()[static_cast<std::size_t>(coulombOctree.GetRootIndex())];
    assert(std::abs(neutralRoot.PositiveCharge - 1.0) < 1.0e-12);
    assert(std::abs(neutralRoot.NegativeChargeMagnitude - 1.0) < 1.0e-12);
    assert(std::abs(neutralRoot.PositiveCenter[0] + 1.0) < 1.0e-12);
    assert(std::abs(neutralRoot.NegativeCenter[0] - 1.0) < 1.0e-12);

    // Barnes-Hut CoulombをDirect法と比較します。小さいthetaでは誤差を抑え、
    // thetaを緩めるとaggregate受理によってpair candidateが減ることを確認します。
    std::vector<CoulombOctreeBody> coulombSolverBodies;
    for (int z = 0; z < 3; ++z)
    {
        for (int y = 0; y < 3; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                CoulombOctreeBody body{};
                body.Position = {
                    static_cast<double>(x) * 1.7 - 2.5,
                    static_cast<double>(y) * 1.3 - 1.2,
                    static_cast<double>(z) * 1.9 - 1.8
                };
                body.ChargeCoulombs = ((x + y + z) % 2 == 0) ? 1.0e-6 : -0.75e-6;
                coulombSolverBodies.push_back(body);
            }
        }
    }

    std::vector<math::Vec3> directCoulombForces(coulombSolverBodies.size(), math::Vec3{});
    for (std::size_t targetIndex = 0u; targetIndex < coulombSolverBodies.size(); ++targetIndex)
    {
        for (std::size_t sourceIndex = 0u; sourceIndex < coulombSolverBodies.size(); ++sourceIndex)
        {
            if (sourceIndex == targetIndex)
            {
                continue;
            }
            directCoulombForces[targetIndex] += ComputeCoulombForce(
                math::Vec3(
                    static_cast<float>(coulombSolverBodies[sourceIndex].Position[0]),
                    static_cast<float>(coulombSolverBodies[sourceIndex].Position[1]),
                    static_cast<float>(coulombSolverBodies[sourceIndex].Position[2])),
                coulombSolverBodies[sourceIndex].ChargeCoulombs,
                math::Vec3(
                    static_cast<float>(coulombSolverBodies[targetIndex].Position[0]),
                    static_cast<float>(coulombSolverBodies[targetIndex].Position[1]),
                    static_cast<float>(coulombSolverBodies[targetIndex].Position[2])),
                coulombSolverBodies[targetIndex].ChargeCoulombs);
        }
    }

    BarnesHutCoulombSolver coulombBarnesHutSolver;
    coulombBarnesHutSolver.SetTheta(0.25);
    CoulombBarnesHutStatistics tightCoulombStatistics{};
    std::vector<math::Vec3> tightCoulombForces;
    coulombBarnesHutSolver.ComputeForces(
        coulombSolverBodies,
        CoulombForceSettings{},
        tightCoulombForces,
        &tightCoulombStatistics);

    double maximumCoulombRelativeError = 0.0;
    for (std::size_t i = 0u; i < directCoulombForces.size(); ++i)
    {
        const double directLength = static_cast<double>(directCoulombForces[i].Length());
        if (directLength <= 1.0e-12)
        {
            continue;
        }
        const double error = static_cast<double>((tightCoulombForces[i] - directCoulombForces[i]).Length());
        maximumCoulombRelativeError = std::max(maximumCoulombRelativeError, error / directLength);
    }
    assert(maximumCoulombRelativeError < 0.1);

    coulombBarnesHutSolver.SetTheta(0.9);
    CoulombBarnesHutStatistics looseCoulombStatistics{};
    std::vector<math::Vec3> looseCoulombForces;
    coulombBarnesHutSolver.ComputeForces(
        coulombSolverBodies,
        CoulombForceSettings{},
        looseCoulombForces,
        &looseCoulombStatistics);
    assert(looseCoulombStatistics.AcceptedAggregateNodeCount > 0u);
    assert(looseCoulombStatistics.PairCandidateCount < tightCoulombStatistics.PairCandidateCount);

    // Coulomb Solver選択設定は不正thetaとhysteresis閾値を正規化します。
    ElectromagneticSystem coulombSelectionSystem;
    CoulombSolverSelectionSettings coulombSelectionSettings{};
    coulombSelectionSettings.Mode = CoulombSolverMode::Automatic;
    coulombSelectionSettings.BarnesHutBodyThreshold = 100u;
    coulombSelectionSettings.DirectBodyThreshold = 120u;
    coulombSelectionSettings.BarnesHutTheta = 0.0;
    coulombSelectionSystem.SetCoulombSolverSelectionSettings(coulombSelectionSettings);
    const CoulombSolverSelectionSettings& normalizedCoulombSelection =
        coulombSelectionSystem.GetCoulombSolverSelectionSettings();
    assert(normalizedCoulombSelection.DirectBodyThreshold == 100u);
    assert(std::abs(normalizedCoulombSelection.BarnesHutTheta - 0.25) < 1.0e-12);

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

    // 一様磁場は位置に依存せず、F=q(v x B)の向きは右手系の外積に従います。
    const UniformMagneticField uniformMagneticField({ 0.0f, 0.0f, 4.0f });
    const math::Vec3 magneticFieldAtOrigin =
        uniformMagneticField.Evaluate({ 0.0f, 0.0f, 0.0f });
    const math::Vec3 magneticFieldFarAway =
        uniformMagneticField.Evaluate({ 100.0f, -50.0f, 25.0f });
    assert(NearlyEqual(magneticFieldFarAway.x, magneticFieldAtOrigin.x));
    assert(NearlyEqual(magneticFieldFarAway.y, magneticFieldAtOrigin.y));
    assert(NearlyEqual(magneticFieldFarAway.z, magneticFieldAtOrigin.z));

    // z軸向きの磁気双極子は、軸上では+z、赤道面では-zの磁場を作ります。
    // 軸上の磁場強度は赤道面の2倍になり、距離を2倍にすると1/8へ減衰します。
    const DipoleMagneticField dipoleMagneticField(
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0e7f }, 0.1f);
    const math::Vec3 dipoleAxisField =
        dipoleMagneticField.Evaluate({ 0.0f, 0.0f, 1.0f });
    const math::Vec3 dipoleEquatorField =
        dipoleMagneticField.Evaluate({ 1.0f, 0.0f, 0.0f });
    const math::Vec3 dipoleAxisFieldAtTwoMeters =
        dipoleMagneticField.Evaluate({ 0.0f, 0.0f, 2.0f });
    assert(dipoleAxisField.z > 0.0f);
    assert(dipoleEquatorField.z < 0.0f);
    assert(NearlyEqual(dipoleAxisField.z / -dipoleEquatorField.z, 2.0f, 1.0e-3f));
    assert(NearlyEqual(dipoleAxisFieldAtTwoMeters.z / dipoleAxisField.z, 0.125f, 1.0e-3f));

    // 双極子中心は方向が定義できないためゼロを返し、MinimumDistance内では有限値を維持します。
    const math::Vec3 dipoleCenterField =
        dipoleMagneticField.Evaluate({ 0.0f, 0.0f, 0.0f });
    const math::Vec3 dipoleSoftenedField =
        dipoleMagneticField.Evaluate({ 0.0f, 0.0f, 0.05f });
    assert(dipoleCenterField.LengthSq() <= 1.0e-12f);
    assert(std::isfinite(dipoleSoftenedField.x));
    assert(std::isfinite(dipoleSoftenedField.y));
    assert(std::isfinite(dipoleSoftenedField.z));

    const math::Vec3 positiveMagneticForce = ComputeMagneticForce(
        2.0, { 3.0f, 0.0f, 0.0f }, magneticFieldAtOrigin);
    const math::Vec3 negativeMagneticForce = ComputeMagneticForce(
        -2.0, { 3.0f, 0.0f, 0.0f }, magneticFieldAtOrigin);
    assert(NearlyEqual(positiveMagneticForce.x, 0.0f));
    assert(NearlyEqual(positiveMagneticForce.y, -24.0f));
    assert(NearlyEqual(positiveMagneticForce.z, 0.0f));
    assert(NearlyEqual(negativeMagneticForce.y, 24.0f));

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

    constexpr float fixedDeltaTime = 1.0f / 60.0f;

    // PhysicsSimulationWorldがElectromagneticSystemを永続所有し、外部Fieldと設定を
    // 複数fixed-step間で保持しながら、毎stepのForceだけを再計算することを確認します。
    UniformElectricField persistentField({ 3.0f, 0.0f, 0.0f });
    Scene persistentFieldScene;
    PhysicsSimulationWorld& persistentSimulationWorld =
        persistentFieldScene.GetPhysicsSimulationWorld();
    ElectromagneticSystem& persistentSystem =
        persistentSimulationWorld.GetElectromagneticSystem();
    const PhysicsSimulationWorld& constPersistentSimulationWorld = persistentSimulationWorld;
    assert(&persistentSystem
        == &constPersistentSimulationWorld.GetElectromagneticSystem());
    assert(persistentSystem.RegisterElectricField(persistentField) == true);
    assert(persistentSystem.RegisterMagneticField(uniformMagneticField) == true);
    assert(persistentSystem.RegisterMagneticField(uniformMagneticField) == false);

    CoulombForceSettings persistentSettings{};
    persistentSettings.CoulombConstant = 1234.0;
    persistentSettings.MinimumDistance = 0.25f;
    persistentSystem.SetCoulombForceSettings(persistentSettings);

    Entity persistentCharge = CreateChargedSphere(
        persistentFieldScene, "Persistent External Field Charge", { 0.0f, 0.0f, 0.0f }, 2.0);
    RigidBodyComponent& persistentBody = persistentCharge.GetComponent<RigidBodyComponent>();
    persistentBody.LinearDamping = 0.0f;
    persistentBody.AngularDamping = 0.0f;

    persistentSimulationWorld.StepSimulation(persistentFieldScene, fixedDeltaTime);
    assert(NearlyEqual(persistentBody.LinearVelocity.x, 6.0f * fixedDeltaTime));
    assert(persistentBody.Force.LengthSq() <= 1.0e-12f);
    assert(persistentSystem.GetRegisteredElectricFieldCount() == 1u);

    persistentSimulationWorld.StepSimulation(persistentFieldScene, fixedDeltaTime);
    assert(NearlyEqual(persistentBody.LinearVelocity.x, 12.0f * fixedDeltaTime));
    assert(NearlyEqual(persistentBody.LinearVelocity.y, -0.8f * fixedDeltaTime));
    assert(persistentBody.Force.LengthSq() <= 1.0e-12f);
    assert(persistentSystem.ContainsElectricField(persistentField) == true);
    assert(persistentSystem.GetCoulombForceSettings().CoulombConstant == 1234.0);
    assert(NearlyEqual(persistentSystem.GetCoulombForceSettings().MinimumDistance, 0.25f));
    assert(persistentSystem.UnregisterElectricField(persistentField) == true);
    assert(persistentSystem.UnregisterMagneticField(uniformMagneticField) == true);
    assert(persistentSystem.UnregisterMagneticField(uniformMagneticField) == false);

    // 磁気ローレンツ力もPhysicsSimulationWorldのfixed-step入口からForceとして積分されます。
    UniformMagneticField integratedMagneticField({ 0.0f, 0.0f, 4.0f });
    Scene magneticScene;
    ElectromagneticSystem& magneticSystem =
        magneticScene.GetPhysicsSimulationWorld().GetElectromagneticSystem();
    assert(magneticSystem.RegisterMagneticField(integratedMagneticField) == true);
    Entity magneticCharge = CreateChargedSphere(
        magneticScene, "Magnetic Field Charge", { 0.0f, 0.0f, 0.0f }, 2.0);
    RigidBodyComponent& magneticBody = magneticCharge.GetComponent<RigidBodyComponent>();
    magneticBody.LinearVelocity = { 3.0f, 0.0f, 0.0f };
    magneticBody.LinearDamping = 0.0f;
    magneticBody.AngularDamping = 0.0f;

    magneticScene.GetPhysicsSimulationWorld().StepSimulation(magneticScene, fixedDeltaTime);
    assert(NearlyEqual(magneticBody.LinearVelocity.x, 3.0f));
    assert(NearlyEqual(magneticBody.LinearVelocity.y, -24.0f * fixedDeltaTime));
    assert(NearlyEqual(magneticBody.LinearVelocity.z, 0.0f));
    assert(magneticBody.Force.LengthSq() <= 1.0e-12f);
    assert(magneticSystem.ContainsMagneticField(integratedMagneticField) == true);
    magneticSystem.ClearMagneticFields();
    assert(magneticSystem.GetRegisteredMagneticFieldCount() == 0u);

    // DipoleMagneticFieldも既存の非所有Registryからfixed-stepへ入り、Lorentz力として積分されます。
    DipoleMagneticField integratedDipoleField(
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0e7f }, 0.1f);
    Scene dipoleScene;
    ElectromagneticSystem& dipoleSystem =
        dipoleScene.GetPhysicsSimulationWorld().GetElectromagneticSystem();
    assert(dipoleSystem.RegisterMagneticField(integratedDipoleField) == true);
    Entity dipoleCharge = CreateChargedSphere(
        dipoleScene, "Dipole Magnetic Field Charge", { 1.0f, 0.0f, 0.0f }, 2.0);
    RigidBodyComponent& dipoleBody = dipoleCharge.GetComponent<RigidBodyComponent>();
    dipoleBody.LinearVelocity = { 0.0f, 1.0f, 0.0f };
    dipoleBody.LinearDamping = 0.0f;
    dipoleBody.AngularDamping = 0.0f;

    const math::Vec3 dipoleFieldAtBody = integratedDipoleField.Evaluate({ 1.0f, 0.0f, 0.0f });
    const math::Vec3 expectedDipoleForce =
        ComputeMagneticForce(2.0, dipoleBody.LinearVelocity, dipoleFieldAtBody);
    dipoleScene.GetPhysicsSimulationWorld().StepSimulation(dipoleScene, fixedDeltaTime);
    assert(NearlyEqual(
        dipoleBody.LinearVelocity.x,
        expectedDipoleForce.x * fixedDeltaTime,
        1.0e-4f));
    assert(NearlyEqual(dipoleBody.LinearVelocity.y, 1.0f, 1.0e-4f));
    assert(dipoleBody.Force.LengthSq() <= 1.0e-12f);
    assert(dipoleSystem.UnregisterMagneticField(integratedDipoleField) == true);

    // Phase 7の外力合成契約を検証します。Astro重力・Electric・Magnetic・Coulombは
    // いずれもRigid積分前の同じForce accumulatorへ加算され、優先順位を持ちません。
    Scene combinedForceScene;
    PhysicsSimulationWorld& combinedSimulationWorld =
        combinedForceScene.GetPhysicsSimulationWorld();
    AstroGravitySolverSelectionSettings combinedGravitySelection{};
    combinedGravitySelection.Mode = AstroGravitySolverMode::Direct;
    combinedSimulationWorld.GetAstroWorld().SetGravitySolverSelectionSettings(combinedGravitySelection);
    GravitySolverSettings combinedGravitySettings{};
    combinedGravitySettings.GravitationalConstant = 1.0;
    combinedGravitySettings.MinimumDistance = 0.01;
    combinedSimulationWorld.GetAstroWorld().SetGravitySolverSettings(combinedGravitySettings);

    UniformElectricField combinedElectricField({ 0.0f, 2.0f, 0.0f });
    UniformMagneticField combinedMagneticField({ 0.0f, 0.0f, 1.0f });
    ElectromagneticSystem& combinedElectromagneticSystem =
        combinedSimulationWorld.GetElectromagneticSystem();
    assert(combinedElectromagneticSystem.RegisterElectricField(combinedElectricField) == true);
    assert(combinedElectromagneticSystem.RegisterMagneticField(combinedMagneticField) == true);
    CoulombForceSettings combinedCoulombSettings{};
    combinedCoulombSettings.CoulombConstant = 1.0;
    combinedCoulombSettings.MinimumDistance = 0.01f;
    combinedElectromagneticSystem.SetCoulombForceSettings(combinedCoulombSettings);

    Entity combinedA = CreateChargedSphere(
        combinedForceScene, "Combined Force A", { 0.0f, 0.0f, 0.0f }, 1.0);
    Entity combinedB = CreateChargedSphere(
        combinedForceScene, "Combined Force B", { 2.0f, 0.0f, 0.0f }, 1.0);
    RigidBodyComponent& combinedBodyA = combinedA.GetComponent<RigidBodyComponent>();
    RigidBodyComponent& combinedBodyB = combinedB.GetComponent<RigidBodyComponent>();
    combinedBodyA.LinearVelocity = { 1.0f, 0.0f, 0.0f };
    combinedBodyB.LinearVelocity = { 1.0f, 0.0f, 0.0f };
    combinedBodyA.LinearDamping = 0.0f;
    combinedBodyB.LinearDamping = 0.0f;
    combinedBodyA.AngularDamping = 0.0f;
    combinedBodyB.AngularDamping = 0.0f;
    combinedA.AddComponent<CelestialBodyComponent>(CelestialBodyComponent{});
    combinedB.AddComponent<CelestialBodyComponent>(CelestialBodyComponent{});

    // mass=1, G=1, r=2なのでGravityはAへ+xに0.25、Coulombは同符号なので-xに0.25で相殺します。
    // Electricは+yに2、v=(1,0,0), B=(0,0,1)のLorentz力は-yに1なので、合成加速度は+yに1です。
    combinedSimulationWorld.StepSimulation(combinedForceScene, fixedDeltaTime);
    assert(NearlyEqual(combinedBodyA.LinearVelocity.x, 1.0f, 1.0e-4f));
    assert(NearlyEqual(combinedBodyA.LinearVelocity.y, fixedDeltaTime, 1.0e-4f));
    assert(NearlyEqual(combinedBodyA.LinearVelocity.z, 0.0f, 1.0e-4f));
    assert(NearlyEqual(combinedBodyB.LinearVelocity.x, 1.0f, 1.0e-4f));
    assert(NearlyEqual(combinedBodyB.LinearVelocity.y, fixedDeltaTime, 1.0e-4f));
    assert(combinedBodyA.Force.LengthSq() <= 1.0e-12f);
    assert(combinedBodyB.Force.LengthSq() <= 1.0e-12f);

    // 天体Dipole BindingはEntityのworld位置とlocal-space磁気モーメントの回転を
    // Magnetic Force評価直前にFieldへ同期します。表示Scaleは磁気モーメントへ影響させません。
    Scene movingDipoleScene;
    PhysicsSimulationWorld& movingDipoleWorld = movingDipoleScene.GetPhysicsSimulationWorld();
    Entity dipoleSource = movingDipoleScene.CreateEntity("Moving Dipole Source");
    TransformComponent& dipoleSourceTransform = dipoleSource.GetComponent<TransformComponent>();
    dipoleSourceTransform.Position = { 3.0f, 4.0f, 5.0f };
    dipoleSourceTransform.Rotation = { 0.0f, 0.0f, 1.57079632679f };
    dipoleSourceTransform.Scale = { 10.0f, 20.0f, 30.0f };

    DipoleMagneticField movingDipoleField;
    CelestialDipoleMagneticFieldBinding movingDipoleBinding{};
    movingDipoleBinding.SourceEntity = dipoleSource.GetHandle();
    movingDipoleBinding.TargetField = &movingDipoleField;
    movingDipoleBinding.LocalDipoleMoment = { 2.0f, 0.0f, 0.0f };
    assert(movingDipoleWorld.RegisterCelestialDipoleMagneticFieldBinding(movingDipoleBinding) == true);
    assert(movingDipoleWorld.RegisterCelestialDipoleMagneticFieldBinding(movingDipoleBinding) == false);
    assert(movingDipoleWorld.GetCelestialDipoleMagneticFieldBindingCount() == 1u);
    assert(movingDipoleWorld.GetElectromagneticSystem().RegisterMagneticField(movingDipoleField) == true);

    movingDipoleWorld.StepSimulation(movingDipoleScene, fixedDeltaTime);
    assert(NearlyEqual(movingDipoleField.GetCenter().x, 3.0f));
    assert(NearlyEqual(movingDipoleField.GetCenter().y, 4.0f));
    assert(NearlyEqual(movingDipoleField.GetCenter().z, 5.0f));
    assert(NearlyEqual(movingDipoleField.GetDipoleMoment().x, 0.0f, 1.0e-4f));
    assert(NearlyEqual(movingDipoleField.GetDipoleMoment().y, 2.0f, 1.0e-4f));
    assert(NearlyEqual(movingDipoleField.GetDipoleMoment().z, 0.0f, 1.0e-4f));
    movingDipoleScene.DestroyEntity(dipoleSource);
    movingDipoleWorld.StepSimulation(movingDipoleScene, fixedDeltaTime);
    assert(movingDipoleField.GetDipoleMoment().LengthSq() <= 1.0e-12f);
    assert(movingDipoleWorld.UnregisterCelestialDipoleMagneticFieldBinding(movingDipoleField) == true);
    assert(movingDipoleWorld.UnregisterCelestialDipoleMagneticFieldBinding(movingDipoleField) == false);
    assert(movingDipoleWorld.GetElectromagneticSystem().UnregisterMagneticField(movingDipoleField) == true);

    // 天体姿勢の変更が次fixed-stepのLorentz力へ反映されることまで統合検証します。
    // local +Z momentを初回はworld +Z、Y軸90度回転後はworld +Xへ向けます。
    Scene orbitingChargeScene;
    PhysicsSimulationWorld& orbitingChargeWorld = orbitingChargeScene.GetPhysicsSimulationWorld();
    Entity rotatingMagneticBody = orbitingChargeScene.CreateEntity("Rotating Magnetic Body");
    rotatingMagneticBody.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };

    DipoleMagneticField rotatingDipoleField(
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0e7f }, 0.1f);
    CelestialDipoleMagneticFieldBinding rotatingDipoleBinding{};
    rotatingDipoleBinding.SourceEntity = rotatingMagneticBody.GetHandle();
    rotatingDipoleBinding.TargetField = &rotatingDipoleField;
    rotatingDipoleBinding.LocalDipoleMoment = { 0.0f, 0.0f, 1.0e7f };
    assert(orbitingChargeWorld.RegisterCelestialDipoleMagneticFieldBinding(rotatingDipoleBinding) == true);
    assert(orbitingChargeWorld.GetElectromagneticSystem().RegisterMagneticField(rotatingDipoleField) == true);

    Entity orbitingCharge = CreateChargedSphere(
        orbitingChargeScene, "Orbiting Test Charge", { 1.0f, 0.0f, 0.0f }, 1.0);
    RigidBodyComponent& orbitingBody = orbitingCharge.GetComponent<RigidBodyComponent>();
    orbitingBody.LinearVelocity = { 0.0f, 1.0f, 0.0f };
    orbitingBody.LinearDamping = 0.0f;
    orbitingBody.AngularDamping = 0.0f;

    orbitingChargeWorld.StepSimulation(orbitingChargeScene, fixedDeltaTime);
    const float firstStepVelocityX = orbitingBody.LinearVelocity.x;
    assert(firstStepVelocityX < 0.0f);

    // Rigid積分でCharge位置も変化するため、Field方向だけを比較できるよう初期状態へ戻します。
    orbitingCharge.GetComponent<TransformComponent>().Position = { 1.0f, 0.0f, 0.0f };
    orbitingBody.LinearVelocity = { 0.0f, 1.0f, 0.0f };
    // orbitingCharge生成時にTransform格納vectorが再確保される可能性があるため、
    // 生成前の参照を保持せず、回転を書き込む時点でComponentを再取得します。
    rotatingMagneticBody.GetComponent<TransformComponent>().Rotation =
        { 0.0f, 1.57079632679f, 0.0f };
    orbitingChargeWorld.StepSimulation(orbitingChargeScene, fixedDeltaTime);

    assert(NearlyEqual(rotatingDipoleField.GetDipoleMoment().x, 1.0e7f, 1.0f));
    assert(NearlyEqual(rotatingDipoleField.GetDipoleMoment().y, 0.0f, 1.0e-3f));
    assert(NearlyEqual(rotatingDipoleField.GetDipoleMoment().z, 0.0f, 1.0f));
    assert(NearlyEqual(orbitingBody.LinearVelocity.x, 0.0f, 1.0e-4f));
    assert(NearlyEqual(orbitingBody.LinearVelocity.y, 1.0f, 1.0e-4f));
    assert(orbitingBody.LinearVelocity.z < 0.0f);

    assert(orbitingChargeWorld.GetElectromagneticSystem().UnregisterMagneticField(rotatingDipoleField) == true);
    assert(orbitingChargeWorld.UnregisterCelestialDipoleMagneticFieldBinding(rotatingDipoleField) == true);

    Scene integratedScene;
    Entity integratedA = CreateChargedSphere(
        integratedScene, "Integrated Charge A", { -0.5f, 0.0f, 0.0f }, microCoulomb);
    Entity integratedB = CreateChargedSphere(
        integratedScene, "Integrated Charge B", { 0.5f, 0.0f, 0.0f }, microCoulomb);

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
