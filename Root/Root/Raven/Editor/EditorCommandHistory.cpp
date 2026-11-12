#include "Raven/Editor/EditorCommandHistory.h"

#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Scene/Scene.h"

#include <cstddef>
#include <utility>
#include <vector>
#include <limits>
#include <new>
#include <stdexcept>

namespace Raven
{
namespace
{

constexpr std::size_t MaxHistoryCount = 128;

// HistoryがCommandの寿命をunique_ptrで単独所有します。
// Commandを追加してもHistoryは具象型を知らず、全Editor操作を同じ順序でUndo / Redoできます。
std::vector<std::unique_ptr<IEditorCommand>> s_UndoStack;
std::vector<std::unique_ptr<IEditorCommand>> s_RedoStack;

// 現在Editorが操作対象としているSceneです。HistoryはSceneを所有しません。
// Commandの実行時にだけ参照として渡し、Scene切替時には全履歴を破棄します。
Scene* s_ActiveScene = nullptr;
std::uint64_t s_HistoryEpoch = 1;

bool ReserveHistorySlot(std::vector<std::unique_ptr<IEditorCommand>>& stack)
{
    // Document復元後の履歴pushで確保失敗すると、変更だけが残りUndoできなくなります。
    // Commandの実行・取り出し前に移動先を確保し、この確保の失敗時は両Stackと対象を維持します。
    try { stack.reserve(stack.size() + 1); return true; }
    catch (const std::bad_alloc&) { return false; }
    catch (const std::length_error&) { return false; }
}

void TrimUndoHistory()
{
    if (s_UndoStack.size() <= MaxHistoryCount)
    {
        return;
    }

    const std::size_t removeCount = s_UndoStack.size() - MaxHistoryCount;
    s_UndoStack.erase(
        s_UndoStack.begin(),
        s_UndoStack.begin() + static_cast<std::ptrdiff_t>(removeCount));
}

void RecordCommand(std::unique_ptr<IEditorCommand> command)
{
    s_UndoStack.push_back(std::move(command));
    TrimUndoHistory();

    // Undo後に新しい編集を行うと、その時点から過去のRedo分岐は成立しません。
    s_RedoStack.clear();
}

} // namespace

void SetEditorCommandHistoryScene(Scene* scene)
{
    if (s_ActiveScene == scene)
    {
        return;
    }

    // SceneごとにEntity Handle空間とComponent Storageは独立しています。
    // Scene切替を跨いで履歴を残さず、破棄済みSceneの非所有Pointerを使用する経路を閉じます。
    ClearEditorCommandHistory();
    s_ActiveScene = scene;
}

bool ExecuteAndRecordEditorCommand(std::unique_ptr<IEditorCommand> command)
{
    if (command == nullptr
        || s_ActiveScene == nullptr
        || command->CanExecute(*s_ActiveScene) == false)
    {
        return false;
    }

    if (ReserveHistorySlot(s_UndoStack) == false || command->Execute(*s_ActiveScene) == false)
    {
        return false;
    }

    RecordCommand(std::move(command));
    return true;
}

bool RecordAlreadyExecutedEditorCommand(std::unique_ptr<IEditorCommand> command)
{
    if (command == nullptr
        || s_ActiveScene == nullptr
        || command->CanExecute(*s_ActiveScene) == false)
    {
        return false;
    }

    // GizmoはDrag中、画面へ即時反映するためTransformを毎frame直接更新しています。
    // Mouse Release時点でExecuteすると最終値を二重適用するため、ここでは検証と履歴登録だけを行います。
    if (ReserveHistorySlot(s_UndoStack) == false) { return false; }
    RecordCommand(std::move(command));
    return true;
}

bool UndoEditorCommand()
{
    if (s_ActiveScene == nullptr)
    {
        return false;
    }
    if (s_UndoStack.empty() == false && ReserveHistorySlot(s_RedoStack) == false) { return false; }

    while (s_UndoStack.empty() == false)
    {
        std::unique_ptr<IEditorCommand> command = std::move(s_UndoStack.back());
        s_UndoStack.pop_back();

        if (command != nullptr
            && command->Undo(*s_ActiveScene) == true)
        {
            EntityHandle before;
            EntityHandle after;
            if (command->GetRestoredEntityHandles(before, after) == true)
            {
                // Undoの残りとRedo側のCommandが古いGenerationを参照しないよう更新します。
                for (const auto& entry : s_UndoStack)
                {
                    entry->RemapEntityHandle(before, after);
                }
                for (const auto& entry : s_RedoStack)
                {
                    entry->RemapEntityHandle(before, after);
                }
            }
            s_RedoStack.push_back(std::move(command));
            return true;
        }

        if (command != nullptr && command->RetainAfterFailure() == true)
        {
            // pop前の容量を再利用するため、この復元pushは確保しません。
            s_UndoStack.push_back(std::move(command));
            return false;
        }
        // 対象EntityやComponentが消えたCommandは捨て、さらに古い有効Commandを探します。
    }

    return false;
}

bool RedoEditorCommand()
{
    if (s_ActiveScene == nullptr)
    {
        return false;
    }
    if (s_RedoStack.empty() == false && ReserveHistorySlot(s_UndoStack) == false) { return false; }

    while (s_RedoStack.empty() == false)
    {
        std::unique_ptr<IEditorCommand> command = std::move(s_RedoStack.back());
        s_RedoStack.pop_back();

        if (command != nullptr
            && command->Redo(*s_ActiveScene) == true)
        {
            EntityHandle before;
            EntityHandle after;
            if (command->GetRestoredEntityHandles(before, after) == true)
            {
                // CreateのRedoで新Generationへ移った場合、残りのRedoとUndoの参照を更新します。
                for (const auto& entry : s_UndoStack)
                {
                    entry->RemapEntityHandle(before, after);
                }
                for (const auto& entry : s_RedoStack)
                {
                    entry->RemapEntityHandle(before, after);
                }
            }
            s_UndoStack.push_back(std::move(command));
            TrimUndoHistory();
            return true;
        }
        if (command != nullptr && command->RetainAfterFailure() == true)
        {
            s_RedoStack.push_back(std::move(command));
            return false;
        }
    }

    return false;
}

bool CanUndoEditorCommand()
{
    return s_UndoStack.empty() == false;
}

bool CanRedoEditorCommand()
{
    return s_RedoStack.empty() == false;
}

void ClearEditorCommandHistory()
{
    s_UndoStack.clear();
    s_RedoStack.clear();
    // 同じアドレスへ別Sceneが生成されても、破棄前通知で進めた世代は一致しません。
    // wrapによる旧Command復活を避け、枯渇時は0へ固定します。
    if (s_HistoryEpoch != 0)
    {
        s_HistoryEpoch = s_HistoryEpoch == (std::numeric_limits<std::uint64_t>::max)() ? 0 : s_HistoryEpoch + 1;
    }
}

std::uint64_t GetEditorCommandHistoryEpoch() { return s_HistoryEpoch; }

} // namespace Raven
