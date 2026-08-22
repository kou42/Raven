#include "Raven/Editor/Panels/StatisticsPanel.h"

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Window.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

#include <cstdint>

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
            ImGui::Separator();

            if (ImGui::BeginTable(
                    "CPUProfileResults",
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

                    // Depthに応じて字下げすることで、Application -> Scene -> Physicsのような
                    // 入れ子関係を簡易的なツリーとして読み取れるようにします。
                    ImGui::Indent(static_cast<float>(result.Depth) * 12.0f);
                    ImGui::TextUnformatted(result.Name.c_str());
                    ImGui::Unindent(static_cast<float>(result.Depth) * 12.0f);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", result.DurationMilliseconds);
                }

                ImGui::EndTable();
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
