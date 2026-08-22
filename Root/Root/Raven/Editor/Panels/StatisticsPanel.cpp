#include "Raven/Editor/Panels/StatisticsPanel.h"

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Window.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Raven
{
namespace
{
template<class Component>
uint32_t CountComponents(Scene& scene)
{
    uint32_t count = 0;
    for (auto&& entry : scene.View<Component>())
    {
        (void)entry;
        ++count;
    }
    return count;
}

struct CPUProfileAggregate
{
    std::string Name;
    double TotalMilliseconds = 0.0;
    double MaxMilliseconds = 0.0;
    uint32_t CallCount = 0u;
};

// Timerとは別に記録した軽量Counterを同名ごとに集計します。
// CellRegistrationのようなHot loopではTimerを細かく置かず、処理中は整数加算だけを行い、
// Build終了時にまとめてCounterを登録することでProfiler自身による計測誤差を抑えます。
struct CPUCounterAggregate
{
    std::string Name;
    double Total = 0.0;
    double Max = 0.0;
    uint32_t SampleCount = 0u;
};

void BuildCPUProfileAggregates(
    const CPUProfileFrame& frame,
    std::vector<CPUProfileAggregate>& outAggregates)
{
    outAggregates.clear();

    std::unordered_map<std::string, std::size_t> aggregateIndices;
    aggregateIndices.reserve(frame.Results.size());

    for (const CPUProfileResult& result : frame.Results)
    {
        const auto iterator = aggregateIndices.find(result.Name);
        if (iterator == aggregateIndices.end())
        {
            CPUProfileAggregate aggregate{};
            aggregate.Name = result.Name;
            aggregate.TotalMilliseconds = result.DurationMilliseconds;
            aggregate.MaxMilliseconds = result.DurationMilliseconds;
            aggregate.CallCount = 1u;

            aggregateIndices.emplace(aggregate.Name, outAggregates.size());
            outAggregates.push_back(std::move(aggregate));
            continue;
        }

        CPUProfileAggregate& aggregate = outAggregates[iterator->second];
        aggregate.TotalMilliseconds += result.DurationMilliseconds;
        aggregate.MaxMilliseconds = std::max(
            aggregate.MaxMilliseconds,
            result.DurationMilliseconds);
        ++aggregate.CallCount;
    }

    // まず「そのframeでCPU時間を最も消費したScope」を見つけたいので、
    // 合計時間の降順で表示します。Fixed Stepが複数回走った場合もTotalへ加算されます。
    std::sort(
        outAggregates.begin(),
        outAggregates.end(),
        [](const CPUProfileAggregate& a, const CPUProfileAggregate& b)
        {
            return a.TotalMilliseconds > b.TotalMilliseconds;
        });
}

void BuildCPUCounterAggregates(
    const CPUProfileFrame& frame,
    std::vector<CPUCounterAggregate>& outAggregates)
{
    outAggregates.clear();

    std::unordered_map<std::string, std::size_t> aggregateIndices;
    aggregateIndices.reserve(frame.Counters.size());

    for (const CPUProfileCounter& counter : frame.Counters)
    {
        const auto iterator = aggregateIndices.find(counter.Name);
        if (iterator == aggregateIndices.end())
        {
            CPUCounterAggregate aggregate{};
            aggregate.Name = counter.Name;
            aggregate.Total = counter.Value;
            aggregate.Max = counter.Value;
            aggregate.SampleCount = 1u;

            aggregateIndices.emplace(aggregate.Name, outAggregates.size());
            outAggregates.push_back(std::move(aggregate));
            continue;
        }

        CPUCounterAggregate& aggregate = outAggregates[iterator->second];
        aggregate.Total += counter.Value;
        aggregate.Max = std::max(aggregate.Max, counter.Value);
        ++aggregate.SampleCount;
    }

    // Counterは時間順ではなく名前順に固定しておくと、frameごとの値比較がしやすくなります。
    std::sort(
        outAggregates.begin(),
        outAggregates.end(),
        [](const CPUCounterAggregate& a, const CPUCounterAggregate& b)
        {
            return a.Name < b.Name;
        });
}

const CPUProfileAggregate* FindCPUProfileAggregate(
    const std::vector<CPUProfileAggregate>& aggregates,
    const char* name)
{
    for (const CPUProfileAggregate& aggregate : aggregates)
    {
        if (aggregate.Name == name)
        {
            return &aggregate;
        }
    }

    return nullptr;
}

const CPUCounterAggregate* FindCPUCounterAggregate(
    const std::vector<CPUCounterAggregate>& aggregates,
    const char* name)
{
    for (const CPUCounterAggregate& aggregate : aggregates)
    {
        if (aggregate.Name == name)
        {
            return &aggregate;
        }
    }

    return nullptr;
}

void DrawSoftBodyCellSizeComparison(
    const std::vector<CPUProfileAggregate>& profileAggregates,
    const std::vector<CPUCounterAggregate>& counterAggregates)
{
    // ========================================================================
    // SoftBody Cell Size Comparison
    // ========================================================================
    // Spatial Hash Cell Sizeを0.04 / 0.05 / 0.06で比較するときに必要な値だけを抜き出します。
    // 通常のProfiler一覧は詳細調査用として残し、この表は「最適Cell Sizeを決める」ことだけに
    // 目的を絞ります。これにより大量のScope / Counterから毎回対象項目を探す必要がありません。
    const CPUProfileAggregate* particleTriangle = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision");
    const CPUProfileAggregate* hashBuild = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild");
    const CPUProfileAggregate* candidateGeneration = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision.CandidateGeneration");
    const CPUProfileAggregate* narrowPhase = FindCPUProfileAggregate(
        profileAggregates,
        "SoftBody.Solver.ParticleTriangleSelfCollision.NarrowPhase");

    const CPUCounterAggregate* cellSize = FindCPUCounterAggregate(
        counterAggregates,
        "SoftBody.TriangleHash.CellSize");
    const CPUCounterAggregate* registrationCount = FindCPUCounterAggregate(
        counterAggregates,
        "SoftBody.TriangleHash.RegistrationCount");
    const CPUCounterAggregate* cellCandidateCount = FindCPUCounterAggregate(
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

        const auto drawMilliseconds = [](const char* label, const CPUProfileAggregate* aggregate)
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

        const auto drawCounter = [](const char* label, const CPUCounterAggregate* aggregate)
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
} // namespace

void StatisticsPanel::OnImGuiRender(float deltaTime, const Window& window, const Scene* scene)
{
    const float frameTimeMs = deltaTime * 1000.0f;
    const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
    const RendererStatistics& rendererStatistics = Renderer::GetStatistics();

    ImGui::Begin("Raven Debug / Statistics");

    if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
        ImGui::Text("Window: %u x %u", window.GetWidth(), window.GetHeight());
    }

    // CPU Profilerは直前に完了したApplication frameを表示します。
    // 計測中frameを直接参照しないため、Profiler側のvectorへ記録している最中でも
    // Editor UIは完成済みの安定したsnapshotだけを読み取れます。
    if (ImGui::CollapsingHeader("CPU Profiler", ImGuiTreeNodeFlags_DefaultOpen))
    {
        CPUProfiler& profiler = CPUProfiler::Get();
        bool profilerEnabled = profiler.IsEnabled();
        if (ImGui::Checkbox("Enabled##CPUProfiler", &profilerEnabled))
        {
            profiler.SetEnabled(profilerEnabled);
        }

        if (profilerEnabled)
        {
            const CPUProfileFrame& profileFrame = profiler.GetLastFrame();
            ImGui::Text("Profile Frame: %llu",
                static_cast<unsigned long long>(profileFrame.FrameIndex));
            ImGui::Text("CPU Frame: %.3f ms", profileFrame.FrameTimeMilliseconds);
            ImGui::Text("Recorded Scopes: %u", static_cast<uint32_t>(profileFrame.Results.size()));
            ImGui::Text("Recorded Counters: %u", static_cast<uint32_t>(profileFrame.Counters.size()));
            ImGui::Separator();

            // 同名Scopeをframe内で集計します。
            // Physics.FixedStepのように1frame中に複数回呼ばれる処理は、Total / Max / Callsを見ることで
            // 「1回が重い」のか「catch-upで呼び出し回数が増えた」のかを区別できます。
            std::vector<CPUProfileAggregate> aggregates;
            BuildCPUProfileAggregates(profileFrame, aggregates);

            // Counterも一度だけ集計し、Cell Size比較表と通常Counter一覧の両方で共有します。
            // 同じframeを二重走査しないことで、Editor側Profiler表示の余計な処理を避けます。
            std::vector<CPUCounterAggregate> counterAggregates;
            BuildCPUCounterAggregates(profileFrame, counterAggregates);

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

                for (const CPUProfileAggregate& aggregate : aggregates)
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

                    for (const CPUCounterAggregate& aggregate : counterAggregates)
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

                    for (const CPUProfileResult& result : profileFrame.Results)
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
        ImGui::Text("Draw Calls: %u", rendererStatistics.DrawCalls);
        ImGui::Text("Index Count: %u", rendererStatistics.IndexCount);
        ImGui::Text("Triangles: %u", rendererStatistics.TriangleCount);
    }

    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (scene == nullptr)
        {
            ImGui::TextDisabled("No active scene.");
        }
        else
        {
            // 現在のScene::View()は非const APIのみなので、読み取り専用のComponent数集計に限って
            // 一時的にnon-const参照へ戻します。PanelからComponent内容は変更しません。
            // const View()を追加した段階で、このconst_castは削除できます。
            Scene& mutableScene = const_cast<Scene&>(*scene);
            const uint32_t rigidBodyCount = CountComponents<RigidBodyComponent>(mutableScene);
            const uint32_t colliderCount = CountComponents<ColliderComponent>(mutableScene);

            const ph::PhysicsWorld& physicsWorld = scene->GetPhysicsWorld();
            const ph::PhysicsSolverDebugStatistics& solverStatistics = physicsWorld.GetSolverDebugStatistics();

            ImGui::Text("Rigid Bodies: %u", rigidBodyCount);
            ImGui::Text("Colliders: %u", colliderCount);
            ImGui::Text("Broad Phase Pairs: %u",
                static_cast<uint32_t>(physicsWorld.GetBroadPhasePairs().size()));
            ImGui::Text("Contact Manifolds: %u", solverStatistics.ManifoldCount);
            ImGui::Text("Contact Points: %u", solverStatistics.ContactPointCount);
            ImGui::Text("Warm Started Constraints: %u", solverStatistics.WarmStartedConstraintCount);
            ImGui::Text("Velocity Iterations: %u", solverStatistics.VelocityIterations);
            ImGui::Text("Max Penetration: %.5f", solverStatistics.MaxPenetration);
        }
    }

    ImGui::End();
}

} // namespace Raven
