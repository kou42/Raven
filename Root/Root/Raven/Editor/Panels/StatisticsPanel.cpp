#include "Raven/Editor/Panels/StatisticsPanel.h"

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
    for (auto entry : scene.View<Component>())
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

    // Runtime統計はEditorそのものではなく、現在実行しているApplication frameの状態です。
    // FPSだけを見るのではなくFrame Timeも並べることで、負荷増加をms単位で追跡できます。
    if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
        ImGui::Text("Window: %u x %u", window.GetWidth(), window.GetHeight());
    }

    // RendererStatisticsはRenderCommand直前で記録しているため、Scene本体だけでなく
    // Physics/Animation Debug Overlay等を含む「実際に発行した描画命令」を確認できます。
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
            // View()は非const APIなので、統計PanelからSceneを変更しない契約を保ったまま
            // Component数だけ数えるため、ここで参照を一時的にnon-constへ戻します。
            // 将来Sceneにconst View()が追加されたら、このconst_castは削除できます。
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
