#include "Raven/Editor/Panels/StatisticsSnapshot.h"

#include "Raven/Core/Window.h"
#include "Raven/Physics/Astro/AstroWorld.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace Raven
{
namespace
{
template<class Component>
std::uint32_t CountComponents(Scene& scene)
{
    std::uint32_t count = 0u;
    for (auto&& entry : scene.View<Component>())
    {
        (void)entry;
        ++count;
    }
    return count;
}

const char* GetAstroSolverKindName(ph::AstroGravitySolverKind kind)
{
    switch (kind)
    {
    case ph::AstroGravitySolverKind::Direct:
        return "Direct";
    case ph::AstroGravitySolverKind::BarnesHut:
        return "Barnes-Hut";
    case ph::AstroGravitySolverKind::Custom:
        return "Custom";
    default:
        return "Unknown";
    }
}

const char* GetAstroSolverModeName(ph::AstroGravitySolverMode mode)
{
    switch (mode)
    {
    case ph::AstroGravitySolverMode::Automatic:
        return "Automatic";
    case ph::AstroGravitySolverMode::Direct:
        return "Direct";
    case ph::AstroGravitySolverMode::BarnesHut:
        return "Barnes-Hut";
    default:
        return "Unknown";
    }
}

void BuildCPUProfileAggregates(
    const CPUProfileFrame& frame,
    std::vector<StatisticsProfileAggregate>& outAggregates)
{
    std::unordered_map<std::string, std::size_t> aggregateIndices;
    aggregateIndices.reserve(frame.Results.size());

    for (const CPUProfileResult& result : frame.Results)
    {
        const auto iterator = aggregateIndices.find(result.Name);
        if (iterator == aggregateIndices.end())
        {
            StatisticsProfileAggregate aggregate{};
            aggregate.Name = result.Name;
            aggregate.TotalMilliseconds = result.DurationMilliseconds;
            aggregate.MaxMilliseconds = result.DurationMilliseconds;
            aggregate.CallCount = 1u;
            aggregateIndices.emplace(aggregate.Name, outAggregates.size());
            outAggregates.push_back(std::move(aggregate));
            continue;
        }

        StatisticsProfileAggregate& aggregate = outAggregates[iterator->second];
        aggregate.TotalMilliseconds += result.DurationMilliseconds;
        aggregate.MaxMilliseconds = std::max(
            aggregate.MaxMilliseconds, result.DurationMilliseconds);
        ++aggregate.CallCount;
    }

    // Fixed Stepが複数回走った場合も合計負荷が大きいScopeから確認できる順序に固定します。
    std::sort(outAggregates.begin(), outAggregates.end(),
        [](const StatisticsProfileAggregate& a, const StatisticsProfileAggregate& b)
        {
            return a.TotalMilliseconds > b.TotalMilliseconds;
        });
}

void BuildCPUCounterAggregates(
    const CPUProfileFrame& frame,
    std::vector<StatisticsCounterAggregate>& outAggregates)
{
    std::unordered_map<std::string, std::size_t> aggregateIndices;
    aggregateIndices.reserve(frame.Counters.size());

    for (const CPUProfileCounter& counter : frame.Counters)
    {
        const auto iterator = aggregateIndices.find(counter.Name);
        if (iterator == aggregateIndices.end())
        {
            StatisticsCounterAggregate aggregate{};
            aggregate.Name = counter.Name;
            aggregate.Total = counter.Value;
            aggregate.Max = counter.Value;
            aggregate.SampleCount = 1u;
            aggregateIndices.emplace(aggregate.Name, outAggregates.size());
            outAggregates.push_back(std::move(aggregate));
            continue;
        }

        StatisticsCounterAggregate& aggregate = outAggregates[iterator->second];
        aggregate.Total += counter.Value;
        aggregate.Max = std::max(aggregate.Max, counter.Value);
        ++aggregate.SampleCount;
    }

    // Counter名順を維持するとFrameを跨いだ視線移動が少なくなります。
    std::sort(outAggregates.begin(), outAggregates.end(),
        [](const StatisticsCounterAggregate& a, const StatisticsCounterAggregate& b)
        {
            return a.Name < b.Name;
        });
}

void CapturePhysicsSnapshot(const Scene* scene, StatisticsPhysicsSnapshot& outSnapshot)
{
    if (scene == nullptr)
    {
        return;
    }

    outSnapshot.Available = true;
    // Scene::View()は現時点で非const APIだけです。Component内容は変更せず件数だけを読みます。
    Scene& mutableScene = const_cast<Scene&>(*scene);
    outSnapshot.RigidBodyCount = CountComponents<RigidBodyComponent>(mutableScene);
    outSnapshot.ColliderCount = CountComponents<ColliderComponent>(mutableScene);

    const ph::PhysicsWorld& physicsWorld = scene->GetPhysicsWorld();
    const ph::PhysicsSolverDebugStatistics& solver = physicsWorld.GetSolverDebugStatistics();
    outSnapshot.BroadPhasePairCount =
        static_cast<std::uint32_t>(physicsWorld.GetBroadPhasePairs().size());
    outSnapshot.ManifoldCount = solver.ManifoldCount;
    outSnapshot.ContactPointCount = solver.ContactPointCount;
    outSnapshot.WarmStartedConstraintCount = solver.WarmStartedConstraintCount;
    outSnapshot.VelocityIterations = solver.VelocityIterations;
    outSnapshot.MaxPenetration = solver.MaxPenetration;

    const ph::AstroWorld& astroWorld =
        scene->GetPhysicsSimulationWorld().GetAstroWorld();
    const ph::AstroStatistics& statistics = astroWorld.GetStatistics();
    const ph::AstroGravitySolverSelectionSettings& settings =
        astroWorld.GetGravitySolverSelectionSettings();
    StatisticsAstroSnapshot& astro = outSnapshot.Astro;
    astro.SolverMode = GetAstroSolverModeName(settings.Mode);
    astro.ActiveSolver = GetAstroSolverKindName(statistics.SolverKind);
    astro.ActiveBodyCount = statistics.ActiveBodyCount;
    astro.BarnesHutBodyThreshold = settings.BarnesHutBodyThreshold;
    astro.DirectBodyThreshold = settings.DirectBodyThreshold;
    astro.BarnesHutTheta = settings.BarnesHutTheta;
    astro.StateCollectionTimeMilliseconds = statistics.StateCollectionTimeMs;
    astro.GravitySolveTimeMilliseconds = statistics.GravitySolveTimeMs;
    astro.GravityTreeBuildTimeMilliseconds = statistics.GravityTreeBuildTimeMs;
    astro.ForceFeedbackTimeMilliseconds = statistics.ForceFeedbackTimeMs;
    astro.GravityForceEvaluationCount = statistics.GravityForceEvaluationCount;
    astro.GravityVisitedNodeCount = statistics.GravityVisitedNodeCount;
    astro.GravityAcceptedAggregateNodeCount =
        statistics.GravityAcceptedAggregateNodeCount;
}
} // namespace

StatisticsSnapshot BuildStatisticsSnapshot(const StatisticsSnapshotInput& input)
{
    StatisticsSnapshot snapshot{};
    const bool validDeltaTime =
        std::isfinite(input.DeltaTime) == true && input.DeltaTime > 0.0f;
    snapshot.Runtime.FrameTimeMilliseconds =
        validDeltaTime == true ? input.DeltaTime * 1000.0f : 0.0f;
    snapshot.Runtime.FramesPerSecond =
        validDeltaTime == true ? 1.0f / input.DeltaTime : 0.0f;
    snapshot.Runtime.WindowWidth = input.WindowWidth;
    snapshot.Runtime.WindowHeight = input.WindowHeight;
    snapshot.Renderer = input.Renderer;

    StatisticsCPUProfilerSnapshot& profiler = snapshot.CPUProfiler;
    profiler.Enabled = input.CPUProfilerEnabled;
    if (profiler.Enabled == true && input.CPUProfile != nullptr)
    {
        const CPUProfileFrame& frame = *input.CPUProfile;
        profiler.FrameIndex = frame.FrameIndex;
        profiler.FrameTimeMilliseconds = frame.FrameTimeMilliseconds;
        profiler.RawResults = frame.Results;
        profiler.RecordedCounterCount = static_cast<std::uint32_t>(frame.Counters.size());
        BuildCPUProfileAggregates(frame, profiler.ProfileAggregates);
        BuildCPUCounterAggregates(frame, profiler.CounterAggregates);
    }

    CapturePhysicsSnapshot(input.ActiveScene, snapshot.Physics);
    return snapshot;
}

StatisticsSnapshot CaptureStatisticsSnapshot(
    float deltaTime, const Window& window, const Scene* scene)
{
    const RendererStatistics& renderer = Renderer::GetStatistics();
    CPUProfiler& profiler = CPUProfiler::Get();
    StatisticsSnapshotInput input{};
    input.DeltaTime = deltaTime;
    input.WindowWidth = window.GetWidth();
    input.WindowHeight = window.GetHeight();
    input.Renderer.DrawCalls = renderer.DrawCalls;
    input.Renderer.IndexCount = renderer.IndexCount;
    input.Renderer.TriangleCount = renderer.TriangleCount;
    input.CPUProfilerEnabled = profiler.IsEnabled();
    input.CPUProfile = &profiler.GetLastFrame();
    input.ActiveScene = scene;
    return BuildStatisticsSnapshot(input);
}

} // namespace Raven
