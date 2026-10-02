#include "Raven/Physics/Astro/Tests/AstroWorldSelfTests.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

#include "Raven/Physics/Astro/AstroWorld.h"
#include "Raven/Physics/Astro/CelestialBody.h"
#include "Raven/Physics/Astro/Gravity/BarnesHutGravitySolver.h"
#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"
#include "Raven/Physics/Astro/Spatial/AstroOctree.h"
#include "Raven/Physics/Spatial/LongRangeOctree.h"
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

struct MultiRateOrbitResult
{
    std::vector<AstroBodyState> Bodies;
    OrbitalDiagnostics InitialDiagnostics{};
    OrbitalDiagnostics FinalDiagnostics{};
};

MultiRateOrbitResult SimulateMultiRateOrbit(
    const std::vector<AstroBodyState>& initialBodies,
    const GravitySolverSettings& settings,
    std::uint32_t farInterval,
    bool adaptive,
    bool smoothing,
    int stepCount,
    double dt)
{
    DirectGravitySolver referenceSolver;
    MultiRateOrbitResult result{};
    result.Bodies = initialBodies;
    result.InitialDiagnostics =
        OrbitalDiagnosticsCalculator::Compute(result.Bodies, settings);

    std::vector<AstroVector3> cachedForces;
    std::vector<AstroVector3> appliedForces;
    std::vector<AstroVector3> transitionStartForces;
    std::uint32_t stepsSinceSolve = 0u;
    std::uint32_t currentInterval = farInterval;
    std::uint32_t stableSteps = 0u;
    std::uint32_t transitionStep = 0u;

    for (int step = 0; step < stepCount; ++step)
    {
        const bool solve =
            cachedForces.size() != result.Bodies.size()
            || currentInterval <= 1u
            || stepsSinceSolve >= (currentInterval - 1u);

        if (solve == true)
        {
            std::vector<AstroVector3> newForces;
            referenceSolver.ComputeForces(result.Bodies, settings, newForces);

            if (smoothing == true && appliedForces.size() == newForces.size())
            {
                transitionStartForces = appliedForces;
                transitionStep = 0u;
            }
            else
            {
                appliedForces = newForces;
                transitionStartForces.clear();
                transitionStep = 0u;
            }
            cachedForces = newForces;
            stepsSinceSolve = 0u;
        }
        else
        {
            ++stepsSinceSolve;
        }

        if (smoothing == true
            && transitionStartForces.size() == cachedForces.size()
            && transitionStep < 2u)
        {
            ++transitionStep;
            const double alpha = static_cast<double>(transitionStep) / 2.0;
            appliedForces.resize(cachedForces.size());
            for (std::size_t i = 0u; i < cachedForces.size(); ++i)
            {
                appliedForces[i] =
                    transitionStartForces[i] * (1.0 - alpha) + cachedForces[i] * alpha;
            }
            if (transitionStep >= 2u)
            {
                transitionStartForces.clear();
            }
        }
        else if (transitionStartForces.empty() == true)
        {
            appliedForces = cachedForces;
        }

        if (adaptive == true)
        {
            std::vector<AstroVector3> directReferenceForces;
            referenceSolver.ComputeForces(result.Bodies, settings, directReferenceForces);
            double maximumRelativeError = 0.0;
            for (std::size_t i = 0u; i < appliedForces.size(); ++i)
            {
                const double referenceMagnitude = directReferenceForces[i].Length();
                if (referenceMagnitude <= 1.0e-12)
                {
                    continue;
                }
                maximumRelativeError = std::max(
                    maximumRelativeError,
                    (appliedForces[i] - directReferenceForces[i]).Length()
                        / referenceMagnitude);
            }

            if (maximumRelativeError >= 0.05)
            {
                stableSteps = 0u;
                if (currentInterval > 1u)
                {
                    --currentInterval;
                }
            }
            else if (maximumRelativeError <= 0.01)
            {
                ++stableSteps;
                if (stableSteps >= 4u && currentInterval < 8u)
                {
                    ++currentInterval;
                    stableSteps = 0u;
                }
            }
            else
            {
                stableSteps = 0u;
            }
        }

        for (std::size_t i = 0u; i < result.Bodies.size(); ++i)
        {
            result.Bodies[i].Velocity += appliedForces[i] * (dt / result.Bodies[i].Mass);
            result.Bodies[i].Position += result.Bodies[i].Velocity * dt;
        }
    }

    result.FinalDiagnostics =
        OrbitalDiagnosticsCalculator::Compute(result.Bodies, settings);
    return result;
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

    // AstroWorldはForceの生成までを担当し、速度積分は通常のRigid経路へ委譲します。
    // PhysicsWorldの積分対象となる完全なRigid Entityにするため、衝突しない十分小さい球を付与します。
    ColliderComponent collider{};
    collider.Type = ColliderType::Sphere;
    collider.Radius = 0.1f;
    entity.AddComponent<ColliderComponent>(collider);

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

    // Gravity/Coulomb共有topologyはDomain固有値を持たず、double位置とPayloadIndexだけを分割します。
    // 非有限位置を除外し、leafから元Domainのindexへ戻せることを確認します。
    std::vector<LongRangeSpatialPoint> sharedSpatialPoints{
        { { -2.0, 0.0, 0.0 }, 10 },
        { { 2.0, 0.0, 0.0 }, 20 },
        { { std::numeric_limits<double>::infinity(), 0.0, 0.0 }, 30 }
    };
    LongRangeOctree sharedOctree;
    sharedOctree.Build(sharedSpatialPoints);
    assert(sharedOctree.GetRootIndex() >= 0);

    std::vector<std::int32_t> sharedPayloadIndices;
    for (const LongRangeOctreeNode& node : sharedOctree.GetNodes())
    {
        if (node.IsLeaf() == false)
        {
            continue;
        }

        for (const std::int32_t pointIndex : node.PointIndices)
        {
            sharedPayloadIndices.push_back(
                sharedSpatialPoints[static_cast<std::size_t>(pointIndex)].PayloadIndex);
        }
    }
    std::sort(sharedPayloadIndices.begin(), sharedPayloadIndices.end());
    assert(sharedPayloadIndices.size() == 2u);
    assert(sharedPayloadIndices[0] == 10);
    assert(sharedPayloadIndices[1] == 20);

    // Octreeのroot集約値が入力Bodyの総質量・重心と一致することを確認します。
    std::vector<AstroBodyState> octreeBodies(4u);
    octreeBodies[0].Mass = 1.0; octreeBodies[0].Position = { -3.0, 0.0, 0.0 };
    octreeBodies[1].Mass = 2.0; octreeBodies[1].Position = { -1.0, 0.0, 0.0 };
    octreeBodies[2].Mass = 3.0; octreeBodies[2].Position = { 1.0, 0.0, 0.0 };
    octreeBodies[3].Mass = 4.0; octreeBodies[3].Position = { 3.0, 0.0, 0.0 };

    AstroOctree octree;
    octree.Build(octreeBodies);
    assert(octree.GetRootIndex() >= 0);
    const AstroOctreeNode& root =
        octree.GetNodes()[static_cast<std::size_t>(octree.GetRootIndex())];
    assert(std::abs(root.TotalMass - 10.0) < 1.0e-12);
    assert(std::abs(root.CenterOfMass.x - 1.0) < 1.0e-12);

    // 完全同一点でもOctreeが無限分割せず、全Bodyの質量を保持することを確認します。
    std::vector<AstroBodyState> coincidentBodies(2u);
    coincidentBodies[0].Mass = 2.0;
    coincidentBodies[0].Position = { 5.0, 5.0, 5.0 };
    coincidentBodies[1].Mass = 3.0;
    coincidentBodies[1].Position = { 5.0, 5.0, 5.0 };
    octree.Build(coincidentBodies);
    const AstroOctreeNode& coincidentRoot =
        octree.GetNodes()[static_cast<std::size_t>(octree.GetRootIndex())];
    assert(std::abs(coincidentRoot.TotalMass - 5.0) < 1.0e-12);

    // Barnes-HutをDirect Solverと比較します。thetaを小さくするとleafまで展開され、
    // Reference Solverに十分近いForceが得られることを最初の採用条件にします。
    std::vector<AstroBodyState> comparisonBodies(32u);
    for (std::size_t i = 0u; i < comparisonBodies.size(); ++i)
    {
        comparisonBodies[i].Mass = 1.0 + static_cast<double>(i % 5u) * 0.1;
        comparisonBodies[i].Position = {
            static_cast<double>(i % 8u) * 1.25,
            static_cast<double>((i / 8u) % 4u) * 1.5,
            static_cast<double>(i) * 0.03125
        };
    }

    std::vector<AstroVector3> directForces;
    std::vector<AstroVector3> barnesHutForces;
    AstroStatistics barnesHutStatistics{};
    solver.ComputeForces(comparisonBodies, orbitSettings, directForces);

    BarnesHutGravitySolver barnesHutSolver;
    double previousMaximumRelativeError = 0.0;
    std::uint64_t previousForceEvaluationCount = 0u;
    bool hasPreviousTheta = false;

    // thetaを大きくするほどaggregate採用が増えて評価数が減る一方、Directとの差は増えます。
    // 絶対時間は実行環境依存なのでSelf Testでは単調なwork量と誤差傾向を検証します。
    for (const double theta : { 0.25, 0.5, 0.75 })
    {
        barnesHutStatistics.Clear();
        barnesHutSolver.SetTheta(theta);
        barnesHutSolver.ComputeForces(
            comparisonBodies,
            orbitSettings,
            barnesHutForces,
            &barnesHutStatistics);

        double maximumRelativeError = 0.0;
        for (std::size_t i = 0u; i < comparisonBodies.size(); ++i)
        {
            const double referenceMagnitude = directForces[i].Length();
            if (referenceMagnitude <= 1.0e-12)
            {
                continue;
            }
            const double relativeError =
                (barnesHutForces[i] - directForces[i]).Length() / referenceMagnitude;
            maximumRelativeError = std::max(maximumRelativeError, relativeError);
        }

        assert(barnesHutStatistics.GravityVisitedNodeCount > 0u);
        assert(barnesHutStatistics.GravityAcceptedAggregateNodeCount > 0u);
        assert(barnesHutStatistics.GravityTreeBuildTimeMs >= 0.0);
        if (theta == 0.25)
        {
            assert(maximumRelativeError < 0.02);
        }

        if (hasPreviousTheta == true)
        {
            assert(barnesHutStatistics.GravityForceEvaluationCount
                <= previousForceEvaluationCount);
            assert(maximumRelativeError + 1.0e-12 >= previousMaximumRelativeError);
        }

        previousMaximumRelativeError = maximumRelativeError;
        previousForceEvaluationCount = barnesHutStatistics.GravityForceEvaluationCount;
        hasPreviousTheta = true;
    }

    // Barnes-Hutの瞬間Force誤差だけでなく、同じ初期状態を複数step積分したときの
    // 軌道・Energy driftもDirect Solverと比較します。近似誤差が時間積分で増幅しても
    // Referenceから大きく逸脱しないことをPhase 5の採用条件として固定します。
    std::vector<AstroBodyState> directOrbitBodies(2u);
    directOrbitBodies[0].Mass = 1.0;
    directOrbitBodies[0].Position = { -1.0, 0.0, 0.0 };
    directOrbitBodies[0].Velocity = { 0.0, -0.5, 0.0 };
    directOrbitBodies[1].Mass = 1.0;
    directOrbitBodies[1].Position = { 1.0, 0.0, 0.0 };
    directOrbitBodies[1].Velocity = { 0.0, 0.5, 0.0 };
    std::vector<AstroBodyState> barnesHutOrbitBodies = directOrbitBodies;

    const OrbitalDiagnostics barnesHutInitialDiagnostics =
        OrbitalDiagnosticsCalculator::Compute(barnesHutOrbitBodies, orbitSettings);
    BarnesHutGravitySolver orbitBarnesHutSolver;
    orbitBarnesHutSolver.SetTheta(0.5);
    std::vector<AstroVector3> directOrbitForces;
    std::vector<AstroVector3> barnesHutOrbitForces;

    for (int step = 0; step < orbitStepCount; ++step)
    {
        solver.ComputeForces(directOrbitBodies, orbitSettings, directOrbitForces);
        orbitBarnesHutSolver.ComputeForces(
            barnesHutOrbitBodies,
            orbitSettings,
            barnesHutOrbitForces);

        for (std::size_t i = 0u; i < directOrbitBodies.size(); ++i)
        {
            directOrbitBodies[i].Velocity +=
                directOrbitForces[i] * (orbitDt / directOrbitBodies[i].Mass);
            directOrbitBodies[i].Position += directOrbitBodies[i].Velocity * orbitDt;

            barnesHutOrbitBodies[i].Velocity +=
                barnesHutOrbitForces[i] * (orbitDt / barnesHutOrbitBodies[i].Mass);
            barnesHutOrbitBodies[i].Position += barnesHutOrbitBodies[i].Velocity * orbitDt;
        }
    }

    const OrbitalDiagnostics barnesHutFinalDiagnostics =
        OrbitalDiagnosticsCalculator::Compute(barnesHutOrbitBodies, orbitSettings);
    const double barnesHutEnergyScale =
        std::max(std::abs(barnesHutInitialDiagnostics.TotalEnergy), 1.0e-12);
    const double barnesHutEnergyRelativeDrift =
        std::abs(barnesHutFinalDiagnostics.TotalEnergy
            - barnesHutInitialDiagnostics.TotalEnergy) / barnesHutEnergyScale;
    const double directBarnesHutPositionError =
        (barnesHutOrbitBodies[0].Position - directOrbitBodies[0].Position).Length();

    // 2-bodyではtargetを含むnodeを必ず展開するため、Barnes-HutはDirectと同じpairを評価します。
    // ここでは長時間積分経路がReferenceと一致し、Energy driftを悪化させないことを確認します。
    assert(directBarnesHutPositionError < 1.0e-10);
    assert(barnesHutEnergyRelativeDrift < 1.0e-5);

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

    // Automaticは収集後の有効Astro body数でSolverを選択します。
    // 小さい閾値を使い、上側境界でBarnes-Hutへ切り替わった後は下側境界まで維持する
    // hysteresis契約を確認します。
    Scene solverSelectionScene;
    AstroWorld solverSelectionWorld;
    AstroGravitySolverSelectionSettings selectionSettings{};
    selectionSettings.BarnesHutBodyThreshold = 3u;
    selectionSettings.DirectBodyThreshold = 2u;
    selectionSettings.BarnesHutTheta = 0.5;
    solverSelectionWorld.SetGravitySolverSelectionSettings(selectionSettings);

    Entity selectionBodyA = CreateCelestialBody(
        solverSelectionScene, "Solver Selection A", { 0.0f, 0.0f, 0.0f }, 1.0f);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::Direct);

    Entity selectionBodyB = CreateCelestialBody(
        solverSelectionScene, "Solver Selection B", { 2.0f, 0.0f, 0.0f }, 1.0f);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::Direct);

    Entity selectionBodyC = CreateCelestialBody(
        solverSelectionScene, "Solver Selection C", { 4.0f, 0.0f, 0.0f }, 1.0f);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::BarnesHut);

    solverSelectionScene.DestroyEntity(selectionBodyC);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::BarnesHut);

    solverSelectionScene.DestroyEntity(selectionBodyB);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::Direct);

    // 強制Modeと既存Custom Solver APIはAutomaticとは独立して選択できます。
    selectionSettings.Mode = AstroGravitySolverMode::Direct;
    solverSelectionWorld.SetGravitySolverSelectionSettings(selectionSettings);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::Direct);

    DirectGravitySolver customSolver;
    solverSelectionWorld.SetGravitySolver(&customSolver);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::Custom);
    solverSelectionWorld.SetGravitySolver(nullptr);
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::Direct);

    selectionSettings.Mode = AstroGravitySolverMode::BarnesHut;
    selectionSettings.DirectBodyThreshold = selectionSettings.BarnesHutBodyThreshold + 1u;
    selectionSettings.BarnesHutTheta = 0.0;
    solverSelectionWorld.SetGravitySolverSelectionSettings(selectionSettings);
    assert(solverSelectionWorld.GetGravitySolverSelectionSettings().DirectBodyThreshold
        == selectionSettings.BarnesHutBodyThreshold);
    assert(NearlyEqual(
        static_cast<float>(solverSelectionWorld.GetGravitySolverSelectionSettings().BarnesHutTheta),
        0.5f));
    solverSelectionWorld.AccumulateGravityForces(solverSelectionScene, 0.1f);
    assert(solverSelectionWorld.GetStatistics().SolverKind == AstroGravitySolverKind::BarnesHut);
    static_cast<void>(selectionBodyA);

    // Phase 8の第一段階として、Gravity Solverの更新周期を落としても毎step同じForceを
    // Rigid accumulatorへ供給できることを確認します。interval=2では solve -> cache -> solve です。
    Scene multiRateScene;
    AstroWorld multiRateWorld;
    multiRateWorld.SetGravitySolverSettings(settings);
    AstroMultiRateSettings multiRateSettings{};
    multiRateSettings.GravityUpdateIntervalSteps = 2u;
    multiRateWorld.SetMultiRateSettings(multiRateSettings);
    Entity multiRateA = CreateCelestialBody(
        multiRateScene, "Multi-rate A", { 0.0f, 0.0f, 0.0f }, 2.0f);
    Entity multiRateB = CreateCelestialBody(
        multiRateScene, "Multi-rate B", { 2.0f, 0.0f, 0.0f }, 3.0f);

    multiRateWorld.AccumulateGravityForces(multiRateScene, 0.1f);
    assert(multiRateWorld.GetStatistics().GravitySolveExecuted == true);
    assert(multiRateWorld.GetStatistics().CachedGravityForceUsed == false);
    const float firstMultiRateForce =
        multiRateA.GetComponent<RigidBodyComponent>().Force.x;

    multiRateA.GetComponent<RigidBodyComponent>().Force = {};
    multiRateB.GetComponent<RigidBodyComponent>().Force = {};
    multiRateWorld.AccumulateGravityForces(multiRateScene, 0.1f);
    assert(multiRateWorld.GetStatistics().GravitySolveExecuted == false);
    assert(multiRateWorld.GetStatistics().CachedGravityForceUsed == true);
    assert(NearlyEqual(
        multiRateA.GetComponent<RigidBodyComponent>().Force.x,
        firstMultiRateForce));

    multiRateA.GetComponent<RigidBodyComponent>().Force = {};
    multiRateB.GetComponent<RigidBodyComponent>().Force = {};
    multiRateWorld.AccumulateGravityForces(multiRateScene, 0.1f);
    assert(multiRateWorld.GetStatistics().GravitySolveExecuted == true);
    assert(multiRateWorld.GetStatistics().CachedGravityForceUsed == false);

    // body集合が変わった場合は更新周期の途中でもcacheを破棄し、Entity対応の誤適用を防ぎます。
    multiRateA.GetComponent<RigidBodyComponent>().Force = {};
    multiRateB.GetComponent<RigidBodyComponent>().Force = {};
    Entity multiRateC = CreateCelestialBody(
        multiRateScene, "Multi-rate C", { 4.0f, 0.0f, 0.0f }, 1.0f);
    multiRateWorld.AccumulateGravityForces(multiRateScene, 0.1f);
    assert(multiRateWorld.GetStatistics().GravitySolveExecuted == true);
    assert(multiRateWorld.GetStatistics().CachedGravityForceUsed == false);
    static_cast<void>(multiRateC);

    multiRateSettings.GravityUpdateIntervalSteps = 0u;
    multiRateWorld.SetMultiRateSettings(multiRateSettings);
    assert(multiRateWorld.GetMultiRateSettings().GravityUpdateIntervalSteps == 1u);

    // Near/Far分離ではNear相互作用を毎step再評価し、Far成分だけを低頻度更新します。
    // G=1、A(m=2)-B(m=3)を2->1へ近づけるとNear Forceは1.5->6.0へ即時変化し、
    // C(m=1, x=10)由来のFar Force 0.02は同じstepではcacheから再利用されます。
    Scene nearFarScene;
    AstroWorld nearFarWorld;
    nearFarWorld.SetGravitySolverSettings(settings);
    AstroGravitySolverSelectionSettings nearFarSolverSettings{};
    nearFarSolverSettings.Mode = AstroGravitySolverMode::Direct;
    nearFarWorld.SetGravitySolverSelectionSettings(nearFarSolverSettings);

    AstroMultiRateSettings nearFarSettings{};
    nearFarSettings.NearGravityDistance = 3.0;
    nearFarSettings.FarGravityUpdateIntervalSteps = 4u;
    nearFarWorld.SetMultiRateSettings(nearFarSettings);

    Entity nearFarA = CreateCelestialBody(
        nearFarScene, "Near/Far A", { 0.0f, 0.0f, 0.0f }, 2.0f);
    Entity nearFarB = CreateCelestialBody(
        nearFarScene, "Near/Far B", { 2.0f, 0.0f, 0.0f }, 3.0f);
    Entity nearFarC = CreateCelestialBody(
        nearFarScene, "Near/Far C", { 10.0f, 0.0f, 0.0f }, 1.0f);

    nearFarWorld.AccumulateGravityForces(nearFarScene, 0.1f);
    assert(nearFarWorld.GetStatistics().FarGravitySolveExecuted == true);
    assert(nearFarWorld.GetStatistics().CachedFarGravityForceUsed == false);
    assert(nearFarWorld.GetStatistics().NearGravityPairEvaluationCount == 1u);
    assert(NearlyEqual(nearFarA.GetComponent<RigidBodyComponent>().Force.x, 1.52f));

    nearFarA.GetComponent<RigidBodyComponent>().Force = {};
    nearFarB.GetComponent<RigidBodyComponent>().Force = {};
    nearFarC.GetComponent<RigidBodyComponent>().Force = {};
    nearFarB.GetComponent<TransformComponent>().Position.x = 1.0f;

    nearFarWorld.AccumulateGravityForces(nearFarScene, 0.1f);
    assert(nearFarWorld.GetStatistics().FarGravitySolveExecuted == false);
    assert(nearFarWorld.GetStatistics().CachedFarGravityForceUsed == true);
    assert(nearFarWorld.GetStatistics().NearGravityPairEvaluationCount == 1u);
    assert(NearlyEqual(nearFarA.GetComponent<RigidBodyComponent>().Force.x, 6.02f));

    // 更新周期4の途中でもBがNear境界を跨いだ場合は、旧Far成分との二重加算を避けるため即時solveします。
    nearFarA.GetComponent<RigidBodyComponent>().Force = {};
    nearFarB.GetComponent<RigidBodyComponent>().Force = {};
    nearFarC.GetComponent<RigidBodyComponent>().Force = {};
    nearFarB.GetComponent<TransformComponent>().Position.x = 4.0f;
    nearFarWorld.AccumulateGravityForces(nearFarScene, 0.1f);
    assert(nearFarWorld.GetStatistics().FarGravitySolveExecuted == true);
    assert(nearFarWorld.GetStatistics().CachedFarGravityForceUsed == false);
    assert(nearFarWorld.GetStatistics().NearGravityPairEvaluationCount == 0u);

    nearFarSettings.FarGravityUpdateIntervalSteps = 0u;
    nearFarSettings.NearGravityDistance = -1.0;
    nearFarWorld.SetMultiRateSettings(nearFarSettings);
    assert(nearFarWorld.GetMultiRateSettings().FarGravityUpdateIntervalSteps == 1u);
    assert(NearlyEqual(
        static_cast<float>(nearFarWorld.GetMultiRateSettings().NearGravityDistance),
        0.0f));

    // Direct Reference診断はRuntime Solverとは別に現在snapshotの正解Forceを求め、
    // Multi-rateで古いForceを使ったstepだけ時間近似誤差が観測できることを確認します。
    Scene lodErrorScene;
    AstroWorld lodErrorWorld;
    lodErrorWorld.SetGravitySolverSettings(settings);
    AstroGravitySolverSelectionSettings lodErrorSolverSettings{};
    lodErrorSolverSettings.Mode = AstroGravitySolverMode::Direct;
    lodErrorWorld.SetGravitySolverSelectionSettings(lodErrorSolverSettings);

    AstroMultiRateSettings lodErrorSettings{};
    lodErrorSettings.GravityUpdateIntervalSteps = 4u;
    lodErrorSettings.MeasureDirectReferenceError = true;
    lodErrorWorld.SetMultiRateSettings(lodErrorSettings);

    Entity lodErrorA = CreateCelestialBody(
        lodErrorScene, "LOD Error A", { 0.0f, 0.0f, 0.0f }, 2.0f);
    Entity lodErrorB = CreateCelestialBody(
        lodErrorScene, "LOD Error B", { 2.0f, 0.0f, 0.0f }, 3.0f);

    lodErrorWorld.AccumulateGravityForces(lodErrorScene, 0.1f);
    assert(lodErrorWorld.GetStatistics().DirectReferenceErrorMeasured == true);
    assert(lodErrorWorld.GetStatistics().MaximumGravityForceRelativeError < 1.0e-12);
    assert(lodErrorWorld.GetStatistics().MeanGravityForceRelativeError < 1.0e-12);
    assert(lodErrorWorld.GetStatistics().MaximumGravityAccelerationError < 1.0e-12);

    lodErrorA.GetComponent<RigidBodyComponent>().Force = {};
    lodErrorB.GetComponent<RigidBodyComponent>().Force = {};
    lodErrorB.GetComponent<TransformComponent>().Position.x = 4.0f;
    lodErrorWorld.AccumulateGravityForces(lodErrorScene, 0.1f);
    assert(lodErrorWorld.GetStatistics().CachedGravityForceUsed == true);
    assert(lodErrorWorld.GetStatistics().DirectReferenceErrorMeasured == true);
    // r=2のcache Force=1.5に対し、現在r=4のReference Force=0.375なので相対誤差は3.0です。
    assert(std::abs(lodErrorWorld.GetStatistics().MaximumGravityForceRelativeError - 3.0)
        < 1.0e-12);
    assert(lodErrorWorld.GetStatistics().MaximumGravityAccelerationError > 0.0);

    // Adaptive LODは誤差がLow以下で安定した場合だけFar周期を伸ばし、
    // High超過時は即座に周期を短くします。ここでは2step安定で2->3、誤差増大で3->2を確認します。
    Scene adaptiveLodScene;
    AstroWorld adaptiveLodWorld;
    adaptiveLodWorld.SetGravitySolverSettings(settings);
    AstroGravitySolverSelectionSettings adaptiveSolverSettings{};
    adaptiveSolverSettings.Mode = AstroGravitySolverMode::Direct;
    adaptiveLodWorld.SetGravitySolverSelectionSettings(adaptiveSolverSettings);

    AstroMultiRateSettings adaptiveSettings{};
    adaptiveSettings.NearGravityDistance = 1.0;
    adaptiveSettings.FarGravityUpdateIntervalSteps = 2u;
    adaptiveSettings.AdaptiveFarGravityUpdate = true;
    adaptiveSettings.MinimumFarGravityUpdateIntervalSteps = 1u;
    adaptiveSettings.MaximumFarGravityUpdateIntervalSteps = 4u;
    adaptiveSettings.AdaptiveFarGravityLowRelativeError = 0.01;
    adaptiveSettings.AdaptiveFarGravityHighRelativeError = 0.10;
    adaptiveSettings.AdaptiveFarGravityStableStepCount = 2u;
    adaptiveLodWorld.SetMultiRateSettings(adaptiveSettings);

    Entity adaptiveA = CreateCelestialBody(
        adaptiveLodScene, "Adaptive LOD A", { 0.0f, 0.0f, 0.0f }, 2.0f);
    Entity adaptiveB = CreateCelestialBody(
        adaptiveLodScene, "Adaptive LOD B", { 4.0f, 0.0f, 0.0f }, 3.0f);

    adaptiveLodWorld.AccumulateGravityForces(adaptiveLodScene, 0.1f);
    assert(adaptiveLodWorld.GetStatistics().CurrentFarGravityUpdateIntervalSteps == 2u);
    assert(adaptiveLodWorld.GetStatistics().AdaptiveFarGravityIntervalChanged == false);

    adaptiveA.GetComponent<RigidBodyComponent>().Force = {};
    adaptiveB.GetComponent<RigidBodyComponent>().Force = {};
    adaptiveLodWorld.AccumulateGravityForces(adaptiveLodScene, 0.1f);
    assert(adaptiveLodWorld.GetStatistics().AdaptiveFarGravityIntervalChanged == true);
    assert(adaptiveLodWorld.GetStatistics().CurrentFarGravityUpdateIntervalSteps == 3u);

    adaptiveA.GetComponent<RigidBodyComponent>().Force = {};
    adaptiveB.GetComponent<RigidBodyComponent>().Force = {};
    adaptiveB.GetComponent<TransformComponent>().Position.x = 8.0f;
    adaptiveLodWorld.AccumulateGravityForces(adaptiveLodScene, 0.1f);
    assert(adaptiveLodWorld.GetStatistics().MaximumGravityForceRelativeError
        > adaptiveSettings.AdaptiveFarGravityHighRelativeError);
    assert(adaptiveLodWorld.GetStatistics().AdaptiveFarGravityIntervalChanged == true);
    assert(adaptiveLodWorld.GetStatistics().CurrentFarGravityUpdateIntervalSteps == 2u);

    // 不正なAdaptive設定は安全な範囲へClampします。
    adaptiveSettings.MinimumFarGravityUpdateIntervalSteps = 0u;
    adaptiveSettings.MaximumFarGravityUpdateIntervalSteps = 0u;
    adaptiveSettings.AdaptiveFarGravityStableStepCount = 0u;
    adaptiveSettings.AdaptiveFarGravityLowRelativeError = -1.0;
    adaptiveSettings.AdaptiveFarGravityHighRelativeError = -1.0;
    adaptiveLodWorld.SetMultiRateSettings(adaptiveSettings);
    assert(adaptiveLodWorld.GetMultiRateSettings().MinimumFarGravityUpdateIntervalSteps == 1u);
    assert(adaptiveLodWorld.GetMultiRateSettings().MaximumFarGravityUpdateIntervalSteps == 1u);
    assert(adaptiveLodWorld.GetMultiRateSettings().AdaptiveFarGravityStableStepCount == 1u);
    assert(adaptiveLodWorld.GetMultiRateSettings().AdaptiveFarGravityLowRelativeError == 0.01);
    assert(adaptiveLodWorld.GetMultiRateSettings().AdaptiveFarGravityHighRelativeError == 0.05);

    // Far Force smoothingは新しいFar solve結果へ即座にjumpせず、直前の適用値から段階遷移します。
    // r=4のForce=0.375からr=2のForce=1.5へ更新し、2step遷移なら最初は中間値0.9375です。
    Scene smoothFarScene;
    AstroWorld smoothFarWorld;
    smoothFarWorld.SetGravitySolverSettings(settings);
    AstroGravitySolverSelectionSettings smoothFarSolverSettings{};
    smoothFarSolverSettings.Mode = AstroGravitySolverMode::Direct;
    smoothFarWorld.SetGravitySolverSelectionSettings(smoothFarSolverSettings);

    AstroMultiRateSettings smoothFarSettings{};
    smoothFarSettings.NearGravityDistance = 1.0;
    smoothFarSettings.FarGravityUpdateIntervalSteps = 2u;
    smoothFarSettings.SmoothFarGravityTransitions = true;
    smoothFarSettings.FarGravityTransitionSteps = 2u;
    smoothFarWorld.SetMultiRateSettings(smoothFarSettings);

    Entity smoothFarA = CreateCelestialBody(
        smoothFarScene, "Smooth Far A", { 0.0f, 0.0f, 0.0f }, 2.0f);
    Entity smoothFarB = CreateCelestialBody(
        smoothFarScene, "Smooth Far B", { 4.0f, 0.0f, 0.0f }, 3.0f);

    smoothFarWorld.AccumulateGravityForces(smoothFarScene, 0.1f);
    assert(NearlyEqual(smoothFarA.GetComponent<RigidBodyComponent>().Force.x, 0.375f));
    assert(smoothFarWorld.GetStatistics().FarGravityTransitionActive == false);

    smoothFarA.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarB.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarWorld.AccumulateGravityForces(smoothFarScene, 0.1f);

    smoothFarA.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarB.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarB.GetComponent<TransformComponent>().Position.x = 2.0f;
    smoothFarWorld.AccumulateGravityForces(smoothFarScene, 0.1f);
    assert(smoothFarWorld.GetStatistics().FarGravitySolveExecuted == true);
    assert(smoothFarWorld.GetStatistics().FarGravityTransitionActive == true);
    assert(std::abs(smoothFarWorld.GetStatistics().FarGravityTransitionAlpha - 0.5) < 1.0e-12);
    assert(NearlyEqual(smoothFarA.GetComponent<RigidBodyComponent>().Force.x, 0.9375f));

    smoothFarA.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarB.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarWorld.AccumulateGravityForces(smoothFarScene, 0.1f);
    assert(smoothFarWorld.GetStatistics().CachedFarGravityForceUsed == true);
    assert(smoothFarWorld.GetStatistics().FarGravityTransitionActive == false);
    assert(std::abs(smoothFarWorld.GetStatistics().FarGravityTransitionAlpha - 1.0) < 1.0e-12);
    assert(NearlyEqual(smoothFarA.GetComponent<RigidBodyComponent>().Force.x, 1.5f));

    // Near/Far境界を跨ぐ強制solveでは旧Far成分を補間せず、分類変更を即時反映します。
    smoothFarA.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarB.GetComponent<RigidBodyComponent>().Force = {};
    smoothFarB.GetComponent<TransformComponent>().Position.x = 0.5f;
    smoothFarWorld.AccumulateGravityForces(smoothFarScene, 0.1f);
    assert(smoothFarWorld.GetStatistics().FarGravitySolveExecuted == true);
    assert(smoothFarWorld.GetStatistics().FarGravityTransitionActive == false);

    smoothFarSettings.FarGravityTransitionSteps = 0u;
    smoothFarWorld.SetMultiRateSettings(smoothFarSettings);
    assert(smoothFarWorld.GetMultiRateSettings().FarGravityTransitionSteps == 1u);

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
