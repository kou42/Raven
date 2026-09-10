#pragma once

#include <string>

namespace Raven
{
class Scene;

// ============================================================================
// AnimationDebugPanel
// ============================================================================
// AnimationRuntimeDebugが生成するRuntime SnapshotをDear ImGuiで可視化するEditor Panelです。
//
// Animationの状態判定やTransition評価をEditor側で再実装すると、RuntimeとEditor表示の結果が
// 食い違う危険があります。そのため、このPanelはAnimatorStateMachine内部を直接解析せず、
// BuildAnimatorStateMachineRuntimeDebugInfo()が返すSnapshotだけを描画入力にします。
//
// Character Locomotionについても同じ方針で、CharacterControllerDemoLocomotionRuntimeが公開する
// Mode / Motion Matching / PoseInertializer診断APIだけを利用します。
//
// 既存AnimationDebugOverlayRendererはEditor導入前のbootstrap表示として残っていますが、
// 今後はこちらを正式なEditor UIとし、Overlayは段階的に役割を縮小していきます。
class AnimationDebugPanel
{
public:
    void OnImGuiRender(Scene* scene);

private:
    int m_SelectedInertializationBoneIndex = 0;
    std::string m_LocomotionRuntimeError;
};

} // namespace Raven
