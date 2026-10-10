#pragma once

namespace Raven
{
class Scene;
class Window;

// ============================================================================
// StatisticsPanel
// ============================================================================
// Editor上でRuntime / Renderer / Physicsの状態を確認するためのPanelです。
//
// 重要:
// Panel自身はSceneやWindowを所有しません。EditorLayerから現在frameの参照を受け取り、
// StatisticsSnapshotへ値をコピーしてから表示します。計測中bufferやSceneへの参照を
// 表示処理へ残さないため、Raven UI版も同じSnapshotを安全に再利用できます。
class StatisticsPanel
{
public:
    void OnImGuiRender(float deltaTime, const Window& window, const Scene* scene);
};

} // namespace Raven
