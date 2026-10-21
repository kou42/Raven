#pragma once

#include "Raven/Core/CPUProfiler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Raven
{

class Scene;
struct UIFrameMetrics;
class Window;

struct StatisticsProfileAggregate
{
    // 同名Scopeが1frame中に複数回走る場合、Total / Max / Callsで
    // 1回の重さとFixed Step catch-upによる回数増加を区別します。
    std::string Name;
    double TotalMilliseconds = 0.0;
    double MaxMilliseconds = 0.0;
    std::uint32_t CallCount = 0u;
};

struct StatisticsCounterAggregate
{
    // Hot loopではTimerを増やさず、整数等を処理末尾でCounter登録することで
    // Profiler自身のmutex・文字列処理による計測汚染を抑えます。
    std::string Name;
    double Total = 0.0;
    double Max = 0.0;
    std::uint32_t SampleCount = 0u;
};

struct StatisticsRuntimeSnapshot
{
    // Delta Timeが未確定または不正なframeでは、0msとして誤表示せず値なしを明示します。
    bool FrameTimingAvailable = false;
    float FramesPerSecond = 0.0f;
    float FrameTimeMilliseconds = 0.0f;
    std::uint32_t WindowWidth = 0u;
    std::uint32_t WindowHeight = 0u;
};

struct StatisticsCPUProfilerSnapshot
{
    bool Enabled = false;
    // CPU Profilerは書き込み中frameではなく、直前に完了したframeだけを公開します。
    // Enabledと分離し、有効化直後の「まだ値なし」をDisabledや0msと区別します。
    bool FrameAvailable = false;
    std::uint64_t FrameIndex = 0u;
    double FrameTimeMilliseconds = 0.0;
    std::vector<StatisticsProfileAggregate> ProfileAggregates;
    std::vector<StatisticsCounterAggregate> CounterAggregates;
    std::vector<CPUProfileResult> RawResults;
    std::uint32_t RecordedCounterCount = 0u;
};

struct StatisticsRendererSnapshot
{
    std::uint32_t DrawCalls = 0u;
    std::uint32_t IndexCount = 0u;
    std::uint32_t TriangleCount = 0u;
};

struct StatisticsUISnapshot
{
    bool Available = false;
    double BuildMilliseconds = 0.0;
    std::uint64_t DrawCommandCount = 0u;
    std::uint64_t VertexCount = 0u;
    std::uint64_t IndexCount = 0u;
    std::uint64_t TextureSwitchCount = 0u;
    std::uint64_t FrameAllocationCount = 0u;
    std::uint64_t FrameAllocatedBytes = 0u;
    std::uint64_t RetainedAllocationBytes = 0u;
    std::uint64_t ElementCount = 0u;
    std::uint64_t PeakElementCount = 0u;
};

struct StatisticsAstroSnapshot
{
    std::string SolverMode;
    std::string ActiveSolver;
    std::uint64_t ActiveBodyCount = 0u;
    std::uint64_t BarnesHutBodyThreshold = 0u;
    std::uint64_t DirectBodyThreshold = 0u;
    double BarnesHutTheta = 0.0;
    double StateCollectionTimeMilliseconds = 0.0;
    double GravitySolveTimeMilliseconds = 0.0;
    double GravityTreeBuildTimeMilliseconds = 0.0;
    double ForceFeedbackTimeMilliseconds = 0.0;
    std::uint64_t GravityForceEvaluationCount = 0u;
    std::uint64_t GravityVisitedNodeCount = 0u;
    std::uint64_t GravityAcceptedAggregateNodeCount = 0u;
};

struct StatisticsPhysicsSnapshot
{
    bool Available = false;
    std::uint32_t RigidBodyCount = 0u;
    std::uint32_t ColliderCount = 0u;
    std::uint32_t BroadPhasePairCount = 0u;
    std::uint32_t ManifoldCount = 0u;
    std::uint32_t ContactPointCount = 0u;
    std::uint32_t WarmStartedConstraintCount = 0u;
    std::uint32_t VelocityIterations = 0u;
    float MaxPenetration = 0.0f;
    StatisticsAstroSnapshot Astro;
};

struct StatisticsSnapshot
{
    StatisticsRuntimeSnapshot Runtime;
    StatisticsCPUProfilerSnapshot CPUProfiler;
    StatisticsRendererSnapshot Renderer;
    StatisticsUISnapshot UI;
    StatisticsPhysicsSnapshot Physics;
};

// UIやglobal singletonから独立した入力です。CPUテストではSceneなし・Profiler無効を
// 明示的に構築でき、表示Backendを増やしても同じSnapshot生成経路を共有できます。
struct StatisticsSnapshotInput
{
    float DeltaTime = 0.0f;
    std::uint32_t WindowWidth = 0u;
    std::uint32_t WindowHeight = 0u;
    StatisticsRendererSnapshot Renderer;
    StatisticsUISnapshot UI;
    bool CPUProfilerEnabled = false;
    const CPUProfileFrame* CPUProfile = nullptr;
    const Scene* ActiveScene = nullptr;
};

StatisticsSnapshot BuildStatisticsSnapshot(const StatisticsSnapshotInput& input);
StatisticsSnapshot CaptureStatisticsSnapshot(
    float deltaTime, const Window& window, const Scene* scene,
    const UIFrameMetrics* uiMetrics = nullptr);

} // namespace Raven
