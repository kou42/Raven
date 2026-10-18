#pragma once

#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Entity.h"

#include <cstdint>
#include <vector>

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

enum class EditorGizmoAxis
{
    None = 0,
    X,
    Y,
    Z
};

// Raven UIへ渡すGizmo描画命令です。GPU handleやWidget参照を持たず、Viewport local座標だけを保持します。
enum class EditorGizmoPrimitiveType
{
    Line = 0,
    Circle,
    Square
};

struct EditorGizmoPrimitive
{
    EditorGizmoPrimitiveType Type = EditorGizmoPrimitiveType::Line;
    math::Vec2 Start{};
    math::Vec2 End{};
    math::Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float Thickness = 1.0f;
    float Radius = 0.0f;
};

struct EditorGizmoViewportRect
{
    math::Vec2 Min{};
    math::Vec2 Max{};
};

enum class EditorGizmoPointerEventType
{
    Down = 0,
    Move,
    Up,
    Cancel
};

struct EditorGizmoPointerEvent
{
    EditorGizmoPointerEventType Type = EditorGizmoPointerEventType::Move;
    math::Vec2 Position{};
    EditorGizmoViewportRect Viewport{};
    bool Snap = false;
};

struct EditorGizmoDrawData
{
    std::vector<EditorGizmoPrimitive> Primitives;
    EditorGizmoAxis HoveredAxis = EditorGizmoAxis::None;
    EditorGizmoAxis ActiveAxis = EditorGizmoAxis::None;
};

EditorGizmoOperation GetEditorGizmoOperation();
void SetEditorGizmoOperation(EditorGizmoOperation operation);

EditorGizmoSpace GetEditorGizmoSpace();
void SetEditorGizmoSpace(EditorGizmoSpace space);
void ToggleEditorGizmoSpace();

const EditorGizmoSnapSettings& GetEditorGizmoSnapSettings();
void SetEditorGizmoSnapSettings(const EditorGizmoSnapSettings& settings);

// Ctrlを押している間だけSnapを有効にします。
// 入力判定は各UI Backend側で行い、この関数は量子化処理だけを共通化します。
float ApplyEditorGizmoSnap(float delta, float step);

// World / Local設定を考慮したGizmo軸方向を取得します。
// axisIndexは0=X / 1=Y / 2=Zです。
math::Vec3 GetEditorGizmoAxisDirection(
    const math::Vec3& rotation,
    int axisIndex);

// Gizmoの座標計算をUI Backendから分離した数値関数です。
bool ProjectEditorGizmoWorldToViewport(
    const math::Vec3& worldPosition,
    const math::Mat4& viewProjection,
    const EditorGizmoViewportRect& viewport,
    math::Vec2& outPosition);
float DistancePointToEditorGizmoSegment(
    const math::Vec2& point,
    const math::Vec2& start,
    const math::Vec2& end);
bool TryBuildEditorGizmoDragPlaneNormal(
    const math::Vec3& axisDirection,
    const math::Vec3& viewDirection,
    math::Vec3& outPlaneNormal);
bool TryIntersectEditorGizmoRayPlane(
    const math::Vec3& rayOrigin,
    const math::Vec3& rayDirection,
    const math::Vec3& planePoint,
    const math::Vec3& planeNormal,
    math::Vec3& outIntersection);
math::Vec3 ApplyEditorGizmoRotationDelta(
    const math::Vec3& startRotation,
    EditorGizmoAxis axis,
    EditorGizmoSpace space,
    float deltaRadians);
float ApplyEditorGizmoMinimumScale(float value, float originalValue);

// Raven UI用のBackend非依存Gizmo状態機械です。
// Entityは各呼び出し中だけ参照し、SceneやUIElementの非所有PointerをFrame間に保持しません。
class EditorTransformGizmoController
{
public:
    EditorGizmoDrawData BuildDrawData(
        Entity selectedEntity,
        const EditorCamera& camera,
        const EditorGizmoViewportRect& viewport,
        const math::Vec2& pointerPosition);

    bool HandlePointerEvent(
        Entity selectedEntity,
        const EditorCamera& camera,
        const EditorGizmoPointerEvent& event);

    bool IsActive() const;
    void Reset();

private:
    EditorGizmoAxis m_ActiveAxis = EditorGizmoAxis::None;
    EditorGizmoOperation m_ActiveOperation = EditorGizmoOperation::Translate;
    EditorGizmoSpace m_ActiveSpace = EditorGizmoSpace::World;
    std::uint64_t m_EntityValue = 0u;
    math::Vec2 m_DragStartMouse{};
    math::Vec2 m_DragStartDirection{};
    math::Vec2 m_ScreenAxisDirection{};
    math::Vec2 m_OriginScreen{};
    math::Vec3 m_DragStartPosition{};
    math::Vec3 m_DragStartRotation{};
    math::Vec3 m_DragStartScale{ 1.0f, 1.0f, 1.0f };
    math::Vec3 m_WorldAxisDirection{};
    math::Vec3 m_DragPlanePoint{};
    math::Vec3 m_DragPlaneNormal{};
    math::Vec3 m_DragStartIntersection{};
    float m_PixelsPerUnit = 0.0f;
    bool m_UseTranslateDragPlane = false;
};

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
