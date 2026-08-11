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
// Panel自身はSceneやWindowを所有しません。Applicationが所有しているRuntime状態を
// EditorLayerから参照として受け取り、「表示する」ことだけを責務にします。
// これにより統計取得のためにEditor専用のコピー状態を増やさず、Engine本体の状態を
// そのまま観測できます。
class StatisticsPanel
{
public:
    void OnImGuiRender(float deltaTime, const Window& window, const Scene* scene);
};

} // namespace Raven
