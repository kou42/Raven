#include "Raven/Editor/Panels/StatisticsPanel.h"
#include "Raven/Editor/Panels/StatisticsSnapshot.h"

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Window.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace Raven
{
namespace
{
const StatisticsProfileAggregate* FindCPUProfileAggregate(
    const std::vector<StatisticsProfileAggregate>& aggregates,
    const char* name)
{
    for (const StatisticsProfileAggregate& aggregate : aggregates)
    {
        if (aggregate.Name == name)
        {
            return &aggregate;
        }
    }

    return nullptr;
}

const StatisticsCounterAggregate* FindCPUCounterAggregate(
    const std::vector<StatisticsCounterAggregate>& aggregates,
    const char* name)
{
    for (const StatisticsCounterAggregate& aggregate : aggregates)
    {
        if (aggregate.Name == name)
        {
            return &aggregate;
        }
    }

    return nullptr;
}

void DrawSoftBodyCellSizeComparison(
    const std::vector<StatisticsProfileAggregate>& profileAggregates,
    const std::vector<StatisticsCounterAggregate>& counterAggregates)
{
    // ========================================================================
    // SoftBody Cell Size Comparison
    // ========================================================================
    // Spatial Hash Cell Sizeを0.04 / 0.05 / 0.06で比較するときに必要な値だけを抜き出します。
    // 通常のProfiler一覧は詳細調査用として残し、この表は「最適Cell Sizeを決める」ことだけに
    // 目的を絞ります。これにより大量のScope / Counterから毎回対象項目を探す必要がありません。
    const StatisticsProfileAggregate* particleTriangle = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision");
    const StatisticsProfileAggregate* hashBuild = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild");
    const StatisticsProfileAggregate* candidateGeneration = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision.CandidateGeneration");
    const StatisticsProfileAggregate* narrowPhase = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision.NarrowPhase");

    const StatisticsCounterAggregate* cellSize = FindCPUCounterAggregate(
        counterAggregates,
        "SoftBody.TriangleHash.CellSize");
    const StatisticsCounterAggregate* registrationCount = FindCPUCounterAggregate(
        counterAggregates,
        "SoftBody.TriangleHash.RegistrationCount");
    const StatisticsCounterAggregate* cellCandidateCount = FindCPUCounterAggregate(
        counterAggregates,
        "SoftBody.TriangleHash.CellCandidateCount");

    if (particleTriangle == nullptr
        && hashBuild == nullptr
        && candidateGeneration == nullptr
        && narrowPhase == nullptr
        && cellSize == nullptr
        && registrationCount == nullptr
        && cellCandidateCount == nullptr)
    {
        ImGui::TextDisabled("No Particle-Triangle self collision profile data in the last frame.");
        return;
    }

    ImGui::TextDisabled(
        "Cell Size comparison focus: same scene / SolverIterations / simulation state recommended.");

    if (ImGui::BeginTable(
            "SoftBodyCellSizeComparison",
            2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        const auto drawMilliseconds = [](const char* label, const StatisticsProfileAggregate* aggregate)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (aggregate != nullptr)
            {
                ImGui::Text("%.3f ms", aggregate->TotalMilliseconds);
            }
            else
            {
                ImGui::TextDisabled("N/A");
            }
        };

        const auto drawCounter = [](const char* label, const StatisticsCounterAggregate* aggregate)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (aggregate != nullptr)
            {
                ImGui::Text("%.3f", aggregate->Total);
            }
            else
            {
                ImGui::TextDisabled("N/A");
            }
        };

        // CellSizeはiterationごとに同じ値がCounter登録されるためTotalではなくAverageを表示します。
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Cell Size");
        ImGui::TableSetColumnIndex(1);
        if (cellSize != nullptr && cellSize->SampleCount > 0u)
        {
            const double averageCellSize =
                cellSize->Total / static_cast<double>(cellSize->SampleCount);
            ImGui::Text("%.3f", averageCellSize);
        }
        else
        {
            ImGui::TextDisabled("N/A");
        }

        drawMilliseconds("ParticleTriangle Total", particleTriangle);
        drawMilliseconds("HashBuild", hashBuild);
        drawMilliseconds("CandidateGeneration", candidateGeneration);
        drawMilliseconds("NarrowPhase", narrowPhase);
        drawCounter("RegistrationCount", registrationCount);
        drawCounter("CellCandidateCount", cellCandidateCount);

        ImGui::EndTable();
    }
}

void DrawFluidSpatialHashComparison(
    const std::vector<StatisticsProfileAggregate>& profileAggregates,
    const std::vector<StatisticsCounterAggregate>& counterAggregates)
{
    // ========================================================================
    // Fluid SPH Spatial Hash Comparison
    // ========================================================================
    // SmoothingRadiusは物理条件として固定し、SpatialHashCellSizeScaleだけを変えて比較します。
    // CellSizeを小さくすると走査Cell数が増え、大きくすると1 Cell内の候補Particleが増えるため、
    // Hash Build時間・候補数・Acceptance Ratioを同時に見て最適値を判断します。
    const StatisticsProfileAggregate* step = FindCPUProfileAggregate(
        profileAggregates,
        "Physics.Fluid.SPH.Step");
    const StatisticsProfileAggregate* spatialHashBuild = FindCPUProfileAggregate(
        profileAggregates,
        "Physics.Fluid.SPH.SpatialHashBuild");
    const StatisticsProfileAggregate* density = FindCPUProfileAggregate(
        profileAggregates,
        "Physics.Fluid.SPH.Density");
    const StatisticsProfileAggregate* force = FindCPUProfileAggregate(
        profileAggregates,
        "Physics.Fluid.SPH.Force");

    const StatisticsCounterAggregate* cellSize = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.SpatialHashCellSize");
    const StatisticsCounterAggregate* occupiedCellCount = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.SpatialHashOccupiedCellCount");
    const StatisticsCounterAggregate* particleCount = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.ParticleCount");
    const StatisticsCounterAggregate* substepCount = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.SubstepCount");
    const StatisticsCounterAggregate* substepLimitReached = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.SubstepLimitReached");
    const StatisticsCounterAggregate* densityCandidateCount = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.DensityNeighborCandidateCount");
    const StatisticsCounterAggregate* densityAcceptedCount = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.DensityNeighborAcceptedCount");
    const StatisticsCounterAggregate* forceCandidateCount = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.ForceNeighborCandidateCount");
    const StatisticsCounterAggregate* forceAcceptedCount = FindCPUCounterAggregate(
        counterAggregates,
        "Physics.Fluid.SPH.ForceNeighborAcceptedCount");

    if (step == nullptr
        && spatialHashBuild == nullptr
        && density == nullptr
        && force == nullptr
        && cellSize == nullptr
        && occupiedCellCount == nullptr)
    {
        ImGui::TextDisabled("No Fluid SPH profile data in the last frame.");
        return;
    }

    ImGui::TextDisabled(
        "Cell Size focus: compare 0.75h / 1.00h / 1.25h with the same initial state and h.");

    if (ImGui::BeginTable(
            "FluidSpatialHashComparison",
            2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        const auto drawMilliseconds = [](const char* label, const StatisticsProfileAggregate* aggregate)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (aggregate != nullptr)
            {
                ImGui::Text("%.3f ms", aggregate->TotalMilliseconds);
            }
            else
            {
                ImGui::TextDisabled("N/A");
            }
        };

        const auto drawTotalCounter = [](const char* label, const StatisticsCounterAggregate* aggregate)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (aggregate != nullptr)
            {
                ImGui::Text("%.0f", aggregate->Total);
            }
            else
            {
                ImGui::TextDisabled("N/A");
            }
        };

        const auto drawAverageCounter = [](const char* label, const StatisticsCounterAggregate* aggregate)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (aggregate != nullptr && aggregate->SampleCount > 0u)
            {
                const double average = aggregate->Total / static_cast<double>(aggregate->SampleCount);
                ImGui::Text("%.3f", average);
            }
            else
            {
                ImGui::TextDisabled("N/A");
            }
        };

        // CellSize / Occupied Cellsは各Substepで同じ意味の標本が追加されるため平均値を表示します。
        // Candidate / AcceptedはFrame中に実行した全Substepの仕事量を見るため合計値を表示します。
        drawAverageCounter("Cell Size", cellSize);
        drawAverageCounter("Occupied Cells", occupiedCellCount);
        drawAverageCounter("Particle Count", particleCount);
        drawTotalCounter("Substeps", substepCount);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Substep Limit Reached");
        ImGui::TableSetColumnIndex(1);
        if (substepLimitReached != nullptr)
        {
            if (substepLimitReached->Max > 0.0)
            {
                ImGui::TextUnformatted("YES");
            }
            else
            {
                ImGui::TextUnformatted("No");
            }
        }
        else
        {
            ImGui::TextDisabled("N/A");
        }

        drawMilliseconds("SPH Step", step);
        drawMilliseconds("Spatial Hash Build", spatialHashBuild);
        drawMilliseconds("Density", density);
        drawMilliseconds("Force", force);

        drawTotalCounter("Density Candidates", densityCandidateCount);
        drawTotalCounter("Density Accepted", densityAcceptedCount);

        // SubstepごとのRatioを単純平均すると、候補数が少ないSubstepも同じ重みになります。
        // Focus表ではAccepted合計 / Candidate合計からFrame全体の加重Acceptanceを再計算します。
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Density Acceptance");
        ImGui::TableSetColumnIndex(1);
        if (densityCandidateCount != nullptr
            && densityAcceptedCount != nullptr
            && densityCandidateCount->Total > 0.0)
        {
            const double ratio = densityAcceptedCount->Total / densityCandidateCount->Total;
            ImGui::Text("%.1f %%", ratio * 100.0);
        }
        else
        {
            ImGui::TextDisabled("N/A");
        }

        drawTotalCounter("Force Candidates", forceCandidateCount);
        drawTotalCounter("Force Accepted", forceAcceptedCount);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Force Acceptance");
        ImGui::TableSetColumnIndex(1);
        if (forceCandidateCount != nullptr
            && forceAcceptedCount != nullptr
            && forceCandidateCount->Total > 0.0)
        {
            const double ratio = forceAcceptedCount->Total / forceCandidateCount->Total;
            ImGui::Text("%.1f %%", ratio * 100.0);
        }
        else
        {
            ImGui::TextDisabled("N/A");
        }

        ImGui::EndTable();
    }
}
} // namespace

void StatisticsPanel::OnImGuiRender(float deltaTime, const Window& window, const Scene* scene)
{
    const StatisticsSnapshot snapshot =
        CaptureStatisticsSnapshot(deltaTime, window, scene);

    ImGui::Begin("Raven Debug / Statistics");

    if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("FPS: %.1f", snapshot.Runtime.FramesPerSecond);
        ImGui::Text("Frame Time: %.3f ms", snapshot.Runtime.FrameTimeMilliseconds);
        ImGui::Text("Window: %u x %u",
            snapshot.Runtime.WindowWidth, snapshot.Runtime.WindowHeight);
    }

    // CPU Profilerは直前に完了したApplication frameを表示します。
    // 計測中frameを直接参照しないため、Profiler側のvectorへ記録している最中でも
    // Editor UIは完成済みの安定したsnapshotだけを読み取れます。
    if (ImGui::CollapsingHeader("CPU Profiler", ImGuiTreeNodeFlags_DefaultOpen))
    {
        CPUProfiler& profiler = CPUProfiler::Get();
        bool profilerEnabled = snapshot.CPUProfiler.Enabled;
        if (ImGui::Checkbox("Enabled##CPUProfiler", &profilerEnabled))
        {
            profiler.SetEnabled(profilerEnabled);
        }

        if (profilerEnabled)
        {
            const StatisticsCPUProfilerSnapshot& profile = snapshot.CPUProfiler;
            ImGui::Text("Profile Frame: %llu",
                static_cast<unsigned long long>(profile.FrameIndex));
            ImGui::Text("CPU Frame: %.3f ms", profile.FrameTimeMilliseconds);
            ImGui::Text("Recorded Scopes: %u",
                static_cast<uint32_t>(profile.RawResults.size()));
            ImGui::Text("Recorded Counters: %u", profile.RecordedCounterCount);
            ImGui::Separator();

            // Snapshot生成時に一度だけ集計し、ImGui版とRaven UI版が同じ値と順序を利用します。
            const auto& aggregates = profile.ProfileAggregates;
            const auto& counterAggregates = profile.CounterAggregates;

            // ====================================================================
            // SoftBody Cell Size Comparison Focus
            // ====================================================================
            // 現在の最適化フェーズで特に注目する7項目だけをCPU Profiler先頭へまとめます。
            // 詳細な全Scope / Counter表示はこの下へ残しているため、必要になった場合は従来どおり
            // 個別Counterまで掘り下げられます。
            if (ImGui::TreeNodeEx("SoftBody Cell Size Comparison", ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawSoftBodyCellSizeComparison(aggregates, counterAggregates);
                ImGui::TreePop();
            }

            // Fluidではhを固定し、SpatialHashCellSizeScaleだけを変えて比較します。
            // SoftBodyと同様に、最適化判断で頻繁に見る値を通常一覧より先にまとめます。
            if (ImGui::TreeNodeEx("Fluid SPH Spatial Hash Comparison", ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFluidSpatialHashComparison(aggregates, counterAggregates);
                ImGui::TreePop();
            }

            ImGui::Separator();

            if (ImGui::BeginTable(
                    "CPUProfileAggregates",
                    4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Scope");
                ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Max ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                for (const StatisticsProfileAggregate& aggregate : aggregates)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(aggregate.Name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", aggregate.TotalMilliseconds);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", aggregate.MaxMilliseconds);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", aggregate.CallCount);
                }

                ImGui::EndTable();
            }

            // ====================================================================
            // Profiler Counters
            // ====================================================================
            // TimerをHot loopへ追加すると計測対象そのものを遅くするため、登録件数・Probe数などは
            // Counterとして別表示します。12 Solver iteration分はTotal / Average / Maxで確認できます。
            if (ImGui::TreeNodeEx("Counters", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable(
                        "CPUProfileCounters",
                        5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Counter");
                    ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableHeadersRow();

                    for (const StatisticsCounterAggregate& aggregate : counterAggregates)
                    {
                        const double average = aggregate.SampleCount > 0u
                            ? aggregate.Total / static_cast<double>(aggregate.SampleCount)
                            : 0.0;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(aggregate.Name.c_str());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.3f", aggregate.Total);

                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.3f", average);

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.3f", aggregate.Max);

                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%u", aggregate.SampleCount);
                    }

                    ImGui::EndTable();
                }

                ImGui::TreePop();
            }

            // 集計値でボトルネックを見つけた後、実際の呼び出し順・入れ子を確認するためのRaw表示です。
            if (ImGui::TreeNode("Raw Scopes"))
            {
                if (ImGui::BeginTable(
                        "CPUProfileRawResults",
                        2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Scope");
                    ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableHeadersRow();

                    for (const CPUProfileResult& result : profile.RawResults)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);

                        ImGui::Indent(static_cast<float>(result.Depth) * 12.0f);
                        ImGui::TextUnformatted(result.Name.c_str());
                        ImGui::Unindent(static_cast<float>(result.Depth) * 12.0f);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.3f", result.DurationMilliseconds);
                    }

                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        }
        else
        {
            ImGui::TextDisabled("CPU profiling is disabled.");
        }
    }

    // RenderCommand直前で記録するため、Scene本体だけでなくDebug Overlay等を含む
    // 「実際に発行した描画命令」を確認できます。
    if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Draw Calls: %u", snapshot.Renderer.DrawCalls);
        ImGui::Text("Index Count: %u", snapshot.Renderer.IndexCount);
        ImGui::Text("Triangles: %u", snapshot.Renderer.TriangleCount);
    }

    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (snapshot.Physics.Available == false)
        {
            ImGui::TextDisabled("No active scene.");
        }
        else
        {
            const StatisticsPhysicsSnapshot& physics = snapshot.Physics;
            ImGui::Text("Rigid Bodies: %u", physics.RigidBodyCount);
            ImGui::Text("Colliders: %u", physics.ColliderCount);
            ImGui::Text("Broad Phase Pairs: %u", physics.BroadPhasePairCount);
            ImGui::Text("Contact Manifolds: %u", physics.ManifoldCount);
            ImGui::Text("Contact Points: %u", physics.ContactPointCount);
            ImGui::Text("Warm Started Constraints: %u", physics.WarmStartedConstraintCount);
            ImGui::Text("Velocity Iterations: %u", physics.VelocityIterations);
            ImGui::Text("Max Penetration: %.5f", physics.MaxPenetration);

            if (ImGui::TreeNodeEx("Astro Gravity", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const StatisticsAstroSnapshot& astro = physics.Astro;

                // SolverKindは直近fixed-stepで実際に使われた値です。
                // Modeと並べることでAutomaticがどちらへ解決されたかを直接確認できます。
                ImGui::Text("Mode: %s", astro.SolverMode.c_str());
                ImGui::Text("Active Solver: %s", astro.ActiveSolver.c_str());
                ImGui::Text("Active Bodies: %llu",
                    static_cast<unsigned long long>(astro.ActiveBodyCount));
                ImGui::Text("Switch Up / Down: %llu / %llu",
                    static_cast<unsigned long long>(astro.BarnesHutBodyThreshold),
                    static_cast<unsigned long long>(astro.DirectBodyThreshold));
                ImGui::Text("Barnes-Hut Theta: %.3f", astro.BarnesHutTheta);
                ImGui::Text("State Collection: %.3f ms", astro.StateCollectionTimeMilliseconds);
                ImGui::Text("Gravity Solve: %.3f ms", astro.GravitySolveTimeMilliseconds);
                ImGui::Text("Octree Build: %.3f ms", astro.GravityTreeBuildTimeMilliseconds);
                ImGui::Text("Force Feedback: %.3f ms", astro.ForceFeedbackTimeMilliseconds);
                ImGui::Text("Force Evaluations: %llu",
                    static_cast<unsigned long long>(astro.GravityForceEvaluationCount));
                ImGui::Text("Node Visits: %llu",
                    static_cast<unsigned long long>(astro.GravityVisitedNodeCount));
                ImGui::Text("Aggregate Nodes: %llu",
                    static_cast<unsigned long long>(astro.GravityAcceptedAggregateNodeCount));
                ImGui::TreePop();
            }
        }
    }

    ImGui::End();
}

} // namespace Raven
