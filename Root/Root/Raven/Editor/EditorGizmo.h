#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class EditorCamera;

// ============================================================================
// EditorGizmoOperation
// ============================================================================
// Scene Viewで現在使用するTransform操作を表します。
// EditorLayerへTranslate/Rotate/Scaleそれぞれの内部状態を持ち込まず、Gizmo機能側で操作モードを
// 一元管理することで、今後Snap等を追加してもEditorLayerを肥大化させません。
enum class EditorGizmoOperation
{
    Translate = 0,
    Rotate,
    Scale
};

// ============================================================================
// EditorGizmoSpace
// ============================================================================
// Gizmo軸をWorld基準で表示・操作するか、選択Entity自身のRotationへ追従するLocal基準で
// 表示・操作するかを表します。
//
// World:
//   X/Y/Zは常にWorldの(1,0,0)/(0,1,0)/(0,0,1)です。
// Local:
//   TransformComponent::RotationからQuaternionを作り、各基準軸をEntity姿勢へ回転させます。
enum class EditorGizmoSpace
{
    World = 0,
    Local
};

// ============================================================================
// EditorGizmoSnapSettings
// ============================================================================
// Transform Snapの刻み幅をGizmo機能側で一元管理します。
// Snapは絶対座標を丸めるのではなく、Drag開始値からの変化量(delta)へ適用します。
// これによりSnapを途中でON/OFFしてもEntityがWorld Gridへ突然飛ばず、掴んだ位置を基準に
// 一定刻みで操作できます。
struct EditorGizmoSnapSettings
{
    float TranslateStep = 0.5f;
    float RotateStepRadians = 0.2617993878f; // 15 degrees
    float ScaleStep = 0.1f;
};

EditorGizmoOperation GetEditorGizmoOperation();
void SetEditorGizmoOperation(EditorGizmoOperation operation);

EditorGizmoSpace GetEditorGizmoSpace();
void SetEditorGizmoSpace(EditorGizmoSpace space);
void ToggleEditorGizmoSpace();

const EditorGizmoSnapSettings& GetEditorGizmoSnapSettings();
void SetEditorGizmoSnapSettings(const EditorGizmoSnapSettings& settings);

// Ctrlを押している間だけSnapを有効にします。
// Dear ImGuiの入力判定自体は各Gizmo側で行い、この関数は量子化処理だけを共通化します。
float ApplyEditorGizmoSnap(float delta, float step);

// World / Local設定を考慮したGizmo軸方向を取得します。
// axisIndexは0=X / 1=Y / 2=Zです。
math::Vec3 GetEditorGizmoAxisDirection(
    const math::Vec3& rotation,
    int axisIndex);

// ============================================================================
// RenderEditorTransformGizmo
// ============================================================================
// 現在選択されている操作モードに対応したGizmoだけをScene Viewへ描画します。
// 非アクティブ側のGizmoへInvalid Entityを渡してDrag状態を明示的にリセットするため、
// 操作途中でTranslate/Rotate/Scaleを切り替えても古いDrag状態が次回の操作へ持ち越されません。
//
// 戻り値:
//   true  : GizmoがMouse入力を消費している
//   false : Entity Picking等がMouse入力を処理してよい
bool RenderEditorTransformGizmo(
    Entity selectedEntity,
    const EditorCamera& camera,
    float viewportMinX,
    float viewportMinY,
    float viewportMaxX,
    float viewportMaxY);

} // namespace Raven
