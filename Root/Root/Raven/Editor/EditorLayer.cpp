#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"

namespace Raven
{

EditorLayer::EditorLayer(Application& application)
    : m_Application(&application)
{
}

void EditorLayer::OnAttach()
{
    // 各PanelはEditorLayerが所有します。
    // 現在のStatisticsPanelは追加初期化不要ですが、Panel固有リソースが必要になった場合も
    // ApplicationではなくEditorLayerのライフサイクルから管理します。
}

void EditorLayer::OnDetach()
{
}

void EditorLayer::OnUpdate(float dt)
{
    (void)dt;

    // Scene View用Editor CameraやEditor独自の更新処理はRuntime Scene更新と分離してここへ追加します。
}

void EditorLayer::OnRender()
{
    // GizmoやEditor用debug primitive等、通常Renderer経由で描画する処理の入口です。
}

void EditorLayer::OnImGuiRender(float dt)
{
    if (m_Application == nullptr)
    {
        return;
    }

    // EditorLayerはPanelとApplicationの橋渡しだけを行います。
    // SceneをPanel内へ永続保持しないため、将来SetScene()でSceneを切り替えても
    // 常にそのframe時点のActive Sceneを表示できます。
    m_StatisticsPanel.OnImGuiRender(
        dt,
        m_Application->GetWindow(),
        m_Application->GetScene());
}

void EditorLayer::OnEvent(Event& event)
{
    (void)event;

    // Editor Camera操作やGizmo操作などの入力は今後ここで処理します。
    // Editor側で入力を消費した場合はevent.Handled = trueとしてRuntime側への伝播を止めます。
}

} // namespace Raven
