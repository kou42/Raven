#include "Raven/Editor/EditorLayer.h"

#include <imgui.h>

namespace Raven
{

void EditorLayer::OnAttach()
{
    // Editor固有リソースの初期化入口です。
    // 現段階ではPanelオブジェクトをまだ持たないため処理はありませんが、
    // 今後StatisticsPanelやSceneHierarchyPanel等を追加する際は、
    // ApplicationではなくこのEditor層から初期化するようにします。
}

void EditorLayer::OnDetach()
{
    // Editor固有リソースの破棄入口です。
    // ImGui Context自体の寿命はImGuiLayerが管理しているため、
    // EditorLayerではPanelやEditor専用リソースだけを解放します。
}

void EditorLayer::OnUpdate(float dt)
{
    (void)dt;

    // Scene View用Editor CameraやEditor独自の更新処理は、
    // Runtime Scene更新とは分離してここから進めます。
}

void EditorLayer::OnRender()
{
    // GizmoやEditor用debug primitive等、通常Renderer経由で描画したいものは
    // 将来的にこのフックから呼び出します。
}

void EditorLayer::OnImGuiRender(float dt)
{
    // EditorのImGui UIはすべてこのLayer以下へ集約します。
    // Application / ImGuiLayerへEditor固有Panelを追加しないことが重要です。
    RenderBootstrapStatistics(dt);
}

void EditorLayer::OnEvent(Event& event)
{
    (void)event;

    // Editor Camera操作やGizmo操作などの入力は今後ここで処理します。
    // LayerはApplicationから逆順でEventを受け取るため、Editor側で入力を消費した場合は
    // event.Handled = true としてRuntime側への伝播を止められます。
}

void EditorLayer::RenderBootstrapStatistics(float deltaTime)
{
    const float frameTimeMs = deltaTime * 1000.0f;
    const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;

    ImGui::Begin("Raven Debug / Statistics");
    ImGui::TextUnformatted("Editor Runtime");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
    ImGui::Separator();
    ImGui::TextDisabled("EditorLayer bootstrap - next step: StatisticsPanel.");
    ImGui::End();
}

} // namespace Raven
