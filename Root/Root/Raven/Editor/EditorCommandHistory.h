#pragma once

#include <memory>

namespace Raven
{

class IEditorCommand;
class Scene;

// ============================================================================
// EditorCommandHistory
// ============================================================================
// Editor操作のUndo / Redo履歴を管理する共通入口です。
//
// IEditorCommandをunique_ptrで保持するため、Transform以外のCommandも同じ履歴へ追加できます。
// Transform GizmoとInspectorの1回のDragを、それぞれ1 Commandとして記録します。
// Drag中の毎frameを履歴へ積むと、Ctrl+Zを何十回も押さないと1操作を戻せなくなるため、
// Gizmo / Inspector側はMouse Buttonを離した時点で「開始Transform / 終了Transform」の1組だけを登録します。
//
// EntityはIndexだけでなくGenerationとSceneも保持して記録します。
// Undo時に対象Entityが破棄され、同じIndexが別Entityへ再利用されていた場合でも、
// Scene::IsEntityAlive()によるGeneration検証で古いCommandを誤適用しません。
//
// Application::SetScene()でScene所有先が差し替わる場合、古いScene*をCommandから
// dereferenceしないことが重要です。SetEditorCommandHistoryScene()で現在Sceneを同期し、
// Sceneが変わった時点で過去SceneのUndo/Redo履歴を破棄します。
void SetEditorCommandHistoryScene(Scene* scene);

// 通常のEditor操作向けです。Commandを実行し、成功した場合だけUndo履歴へ登録します。
bool ExecuteAndRecordEditorCommand(std::unique_ptr<IEditorCommand> command);

// Gizmoのように呼び出し前に変更が適用済みの操作向けです。
// Commandを二重実行せず、現在Sceneで有効なCommandだけをUndo履歴へ登録します。
bool RecordAlreadyExecutedEditorCommand(std::unique_ptr<IEditorCommand> command);

bool UndoEditorCommand();
bool RedoEditorCommand();

bool CanUndoEditorCommand();
bool CanRedoEditorCommand();

// Scene切替やEditor終了時など、過去SceneへのCommandを保持したくない境界で利用します。
void ClearEditorCommandHistory();

} // namespace Raven
