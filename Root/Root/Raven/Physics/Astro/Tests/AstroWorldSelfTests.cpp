#include "Raven/Physics/Astro/Tests/AstroWorldSelfTests.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/Astro/AstroWorld.h"
#include "Raven/Physics/Astro/CelestialBody.h"
#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"
#include "Raven/Physics/Astro/OrbitalDiagnostics.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{
bool NearlyEqual(float left, float right, float epsilon = 1.0e-5f)
{
    return std::abs(left - right) <= epsilon;
}

Entity CreateCelestialBody(
    Scene& scene,
    const char* name,
    const math::Vec3& position,
    float mass,
    BodyType bodyType = BodyType::Dynamic)
{
    Entity entity = scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().Position = position;

    RigidBodyComponent rigidBody{};
    rigidBody.SetMass(mass);
    rigidBody.SetBodyType(bodyType);
    rigidBody.UseGravity = false;
    rigidBody.AllowSleep = false;
    rigidBody.LinearDamping = 0.0f;
    rigidBody.AngularDamping = 0.0f;
    entity.AddComponent<RigidBodyComponent>(rigidBody);
    entity.AddComponent<CelestialBodyComponent>(CelestialBodyComponent{});
    return entity;
}
}

void RunAstroWorldSelfTests()
{
    DirectGravitySolver solver;
    GravitySolverSettings settings{};
    settings.GravitationalConstant = 1.0;
    settings.MinimumDistance = 0.01;

    std::vector<AstroBodyState> bodies(2u);
    bodies[0].Position = { 0.0f, 0.0f, 0.0f };
    bodies[0].Mass = 2.0;
    bodies[1].Position = { 2.0f, 0.0f, 0.0f };
    bodies[1].Mass = 3.0;

    std::vector<AstroVector3> forces;
    AstroStatistics directStatistics{};
    solver.ComputeForces(bodies, settings, forces, &directStatistics);

    // G=1, m1=2, m2=3, r=2 なので |F|=1.5。両Bodyへ同じ大きさを逆向きに加えます。
    assert(forces.size() == 2u);
    assert(directStatistics.GravityPairCandidateCount == 1u);
    assert(directStatistics.GravityForceEvaluationCount == 1u);
    assert(NearlyEqual(forces[0].x, 1.5f));
    assert(NearlyEqual(forces[1].x, -1.5f));
    assert(NearlyEqual(forces[0].x + forces[1].x, 0.0f));

    bodies[1].Position = { 4.0f, 0.0f, 0.0f };
    solver.ComputeForces(bodies, settings, forces);
    assert(NearlyEqual(forces[0].x, 0.375f));

    bodies[1].Mass = 6.0;
    solver.ComputeForces(bodies, settings, forces);
    assert(NearlyEqual(forces[0].x, 0.75f));

    // MinimumDistance内では逆二乗の分母だけをClampし、方向は実際の相対位置から維持します。
    bodies[0].Mass = 2.0;
    bodies[1].Mass = 3.0;
    bodies[1].Position = { 0.001f, 0.0f, 0.0f };
    solver.ComputeForces(bodies, settings, forces);
    assert(NearlyEqual(forces[0].x, 60000.0f, 1.0f));

    bodies[1].Position = bodies[0].Position;
    solver.ComputeForces(bodies, settings, forces);
    assert(std::isfinite(forces[0].x));
    assert(std::isfinite(forces[1].x));
    assert(NearlyEqual(forces[0].LengthSq(), 0.0f));

    // Generate/Receiveを分離したStatic sourceでは、source自身を動かさずprobeだけへ引力を加えられます。
    bodies[0].Position = { 0.0f, 0.0f, 0.0f };
    bodies[0].Mass = 10.0;
    bodies[0].GenerateGravity = true;
    bodies[0].ReceiveGravity = false;
    bodies[1].Position = { 2.0f, 0.0f, 0.0f };
    bodies[1].Mass = 1.0;
    bodies[1].GenerateGravity = false;
    bodies[1].ReceiveGravity = true;
    solver.ComputeForces(bodies, settings, forces);
    assert(NearlyEqual(forces[0].LengthSq(), 0.0f));
    assert(NearlyEqual(forces[1].x, -2.5f));

    // double precision stateがfloat Scene座標では保持できない微小差を維持できることを確認します。
    AstroBodyState precisionBody{};
    precisionBody.Position = { 1000000000000.0, 0.0, 0.0 };
    const AstroVector3 preciseOffset = precisionBody.Position + AstroVector3{ 0.001, 0.0, 0.0 };
    assert(std::abs((preciseOffset.x - precisionBody.Position.x) - 0.001) < 1.0e-4);

    // 等質量2体の円軌道を重心系で積分し、1周期後の半径・Energy・Momentum driftを確認します。
    // v = sqrt(G*m/(4*r))。ここでは各Bodyの重心からの半径r=1、相互距離2です。
    std::vector<AstroBodyState> orbitBodies(2u);
    orbitBodies[0].Mass = 1.0;
    orbitBodies[0].Position = { -1.0, 0.0, 0.0 };
    orbitBodies[0].Velocity = { 0.0, -0.5, 0.0 };
    orbitBodies[1].Mass = 1.0;
    orbitBodies[1].Position = { 1.0, 0.0, 0.0 };
    orbitBodies[1].Velocity = { 0.0, 0.5, 0.0 };

    GravitySolverSettings orbitSettings{};
    orbitSettings.GravitationalConstant = 1.0;
    orbitSettings.MinimumDistance = 1.0e-9;
    const OrbitalDiagnostics initialDiagnostics =
        OrbitalDiagnosticsCalculator::Compute(orbitBodies, orbitSettings);
    const double orbitalPeriod = 4.0 * std::acos(-1.0);
    constexpr int orbitStepCount = 4000;
    const double orbitDt = orbitalPeriod / static_cast<double>(orbitStepCount);
    std::vector<AstroVector3> orbitForces;

    for (int step = 0; step < orbitStepCount; ++step)
    {
        solver.ComputeForces(orbitBodies, orbitSettings, orbitForces);
        for (std::size_t i = 0u; i < orbitBodies.size(); ++i)
        {
            // 現行Rigid Bodyと同じsemi-implicit Eulerをdouble state上で再現し、
            // Astro専用積分器が必要かを比較するためのReferenceにします。
            orbitBodies[i].Velocity += orbitForces[i] * (orbitDt / orbitBodies[i].Mass);
            orbitBodies[i].Position += orbitBodies[i].Velocity * orbitDt;
        }
    }

    const OrbitalDiagnostics finalDiagnostics =
        OrbitalDiagnosticsCalculator::Compute(orbitBodies, orbitSettings);
    const double radiusError = std::abs(orbitBodies[0].Position.Length() - 1.0);
    const double energyScale = std::max(std::abs(initialDiagnostics.TotalEnergy), 1.0e-12);
    const double energyRelativeDrift =
        std::abs(finalDiagnostics.TotalEnergy - initialDiagnostics.TotalEnergy) / energyScale;
    assert(radiusError < 2.0e-3);
    assert(energyRelativeDrift < 1.0e-5);
    assert(finalDiagnostics.TotalLinearMomentum.Length() < 1.0e-10);
    assert((finalDiagnostics.TotalAngularMomentum - initialDiagnostics.TotalAngularMomentum).Length()
        < 1.0e-10);

    // Direct SolverのO(N^2)候補数が N*(N-1)/2 と一致することを複数規模で確認します。
    // 10,000 bodiesは約5千万pairになるためDebug起動時には回さず、実機benchmarkで明示実行します。
    for (const std::size_t bodyCount : { 10u, 100u, 1000u })
    {
        std::vector<AstroBodyState> benchmarkBodies(bodyCount);
        for (std::size_t i = 0u; i < bodyCount; ++i)
        {
            benchmarkBodies[i].Mass = 1.0;
            benchmarkBodies[i].Position = {
                static_cast<double>(i) + 1.0,
                static_cast<double>(i % 7u) * 0.25,
                0.0
            };
        }

        AstroStatistics benchmarkStatistics{};
        std::vector<AstroVector3> benchmarkForces;
        solver.ComputeForces(
            benchmarkBodies,
            orbitSettings,
            benchmarkForces,
            &benchmarkStatistics);

        const std::uint64_t expectedPairCount =
            static_cast<std::uint64_t>(bodyCount)
            * static_cast<std::uint64_t>(bodyCount - 1u) / 2u;
        assert(benchmarkStatistics.GravityPairCandidateCount == expectedPairCount);
        assert(benchmarkStatistics.GravityForceEvaluationCount == expectedPairCount);
    }

    // PhysicsSimulationWorldではAstro重力をElectromagnetismと同様にRigid積分前のForceへ蓄積します。
    Scene scene;
    PhysicsSimulationWorld& simulationWorld = scene.GetPhysicsSimulationWorld();
    GravitySolverSettings worldSettings{};
    worldSettings.GravitationalConstant = 1.0;
    worldSettings.MinimumDistance = 0.01;
    simulationWorld.GetAstroWorld().SetGravitySolverSettings(worldSettings);

    Entity a = CreateCelestialBody(scene, "Astro A", { 0.0f, 0.0f, 0.0f }, 2.0f);
    Entity b = CreateCelestialBody(scene, "Astro B", { 2.0f, 0.0f, 0.0f }, 3.0f);

    constexpr float fixedDeltaTime = 0.1f;
    simulationWorld.StepSimulation(scene, fixedDeltaTime);

    const RigidBodyComponent& rigidBodyA = a.GetComponent<RigidBodyComponent>();
    const RigidBodyComponent& rigidBodyB = b.GetComponent<RigidBodyComponent>();
    assert(NearlyEqual(rigidBodyA.LinearVelocity.x, 0.075f));
    assert(NearlyEqual(rigidBodyB.LinearVelocity.x, -0.05f));
    assert(rigidBodyA.Force.LengthSq() <= 1.0e-12f);
    assert(rigidBodyB.Force.LengthSq() <= 1.0e-12f);
    const OrbitalDiagnostics& worldDiagnostics = simulationWorld.GetAstroWorld().GetLastDiagnostics();
    assert(std::isfinite(worldDiagnostics.TotalEnergy));
    assert(NearlyEqual(static_cast<float>(worldDiagnostics.TotalLinearMomentum.Length()), 0.0f));
    const AstroStatistics& worldStatistics = simulationWorld.GetAstroWorld().GetStatistics();
    assert(worldStatistics.ActiveBodyCount == 2u);
    assert(worldStatistics.GravityPairCandidateCount == 1u);
    assert(worldStatistics.GravityForceEvaluationCount == 1u);
    assert(worldStatistics.StateCollectionTimeMs >= 0.0);
    assert(worldStatistics.GravitySolveTimeMs >= 0.0);
    assert(worldStatistics.ForceFeedbackTimeMs >= 0.0);

    // Static sourceは積分対象外でもMassを保持して重力源になり、Dynamic probeだけを加速します。
    Scene staticSourceScene;
    PhysicsSimulationWorld& staticSourceWorld = staticSourceScene.GetPhysicsSimulationWorld();
    staticSourceWorld.GetAstroWorld().SetGravitySolverSettings(worldSettings);
    Entity source = CreateCelestialBody(
        staticSourceScene, "Static Gravity Source", { 0.0f, 0.0f, 0.0f }, 10.0f, BodyType::Static);
    Entity probe = CreateCelestialBody(
        staticSourceScene, "Gravity Probe", { 2.0f, 0.0f, 0.0f }, 1.0f);
    source.GetComponent<CelestialBodyComponent>().ReceiveGravity = false;
    probe.GetComponent<CelestialBodyComponent>().GenerateGravity = false;

    staticSourceWorld.StepSimulation(staticSourceScene, fixedDeltaTime);
    assert(NearlyEqual(probe.GetComponent<RigidBodyComponent>().LinearVelocity.x, -0.25f));
    assert(NearlyEqual(source.GetComponent<TransformComponent>().Position.x, 0.0f));
}

} // namespace Raven::ph::tests
