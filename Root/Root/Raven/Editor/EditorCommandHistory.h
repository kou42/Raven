#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

struct TransformComponent;

// ============================================================================
// EditorCommandHistory
// ============================================================================
// Editor操作のUndo / Redo履歴を管理する共通入口です。
//
// 現段階ではTransform Gizmoの1回のDragを1 Commandとして記録します。
// Drag中の毎frameを履歴へ積むと、Ctrl+Zを何十回も押さないと1操作を戻せなくなるため、
// Gizmo側はMouse Buttonを離した時点で「開始Transform / 終了Transform」の1組だけを登録します。
//
// EntityはIndexだけでなくGenerationとSceneも保持して記録します。
// Undo時に対象Entityが破棄され、同じIndexが別Entityへ再利用されていた場合でも、
// Scene::IsEntityAlive()によるGeneration検証で古いCommandを誤適用しません。
void RecordEditorTransformCommand(
    Entity entity,
    const TransformComponent& before,
    const TransformComponent& after);

bool UndoEditorCommand();
bool RedoEditorCommand();

bool CanUndoEditorCommand();
bool CanRedoEditorCommand();

// Scene切替やEditor終了時など、過去SceneへのCommandを保持したくない境界で利用します。
void ClearEditorCommandHistory();

} // namespace Raven
