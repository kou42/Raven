#pragma once

#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{

// ============================================================================
// EditorLayer
// ============================================================================
// Raven Editor全体の入口となるLayerです。
//
// ApplicationはWindow / Scene / Layer更新 / ImGui frame境界といった
// 「アプリケーションを動かすための基盤処理」だけを担当し、Editor固有のUIは
// このLayer以下へ分離します。
//
// 今後はEditorLayer自身へ全UI処理を詰め込むのではなく、
// StatisticsPanel / AnimationDebugPanel / SceneHierarchyPanel / InspectorPanel等を
// 個別クラスとして追加し、EditorLayerはそれらを束ねる役割に留めます。
class EditorLayer : public Layer
{
public:
    EditorLayer() = default;
    ~EditorLayer() override = default;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    void OnImGuiRender(float dt) override;
    void OnEvent(Event& event) override;

private:
    // 現在はbootstrap用の最小Statistics表示です。
    // 次の実装段階でStatisticsPanelクラスへ切り出し、Renderer / Physics統計も
    // 同じPanelへ集約します。
    void RenderBootstrapStatistics(float deltaTime);
};

} // namespace Raven
