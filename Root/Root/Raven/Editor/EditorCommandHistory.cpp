#include "Raven/Editor/EditorCommandHistory.h"

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace Raven
{
namespace
{

struct TransformCommand
{
    EntityHandle Handle{};
    Scene* TargetScene = nullptr;
    TransformComponent Before{};
    TransformComponent After{};
};

constexpr std::size_t MaxHistoryCount = 128;
constexpr float TransformCompareEpsilon = 0.000001f;

std::vector<TransformCommand> s_UndoStack;
std::vector<TransformCommand> s_RedoStack;

bool NearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= TransformCompareEpsilon;
}

bool TransformEquals(
    const TransformComponent& a,
    const TransformComponent& b)
{
    return NearlyEqual(a.Position.x, b.Position.x)
        && NearlyEqual(a.Position.y, b.Position.y)
        && NearlyEqual(a.Position.z, b.Position.z)
        && NearlyEqual(a.Rotation.x, b.Rotation.x)
        && NearlyEqual(a.Rotation.y, b.Rotation.y)
        && NearlyEqual(a.Rotation.z, b.Rotation.z)
        && NearlyEqual(a.Scale.x, b.Scale.x)
        && NearlyEqual(a.Scale.y, b.Scale.y)
        && NearlyEqual(a.Scale.z, b.Scale.z);
}

bool TryApplyTransform(
    const TransformCommand& command,
    const TransformComponent& transform)
{
    if (command.TargetScene == nullptr)
    {
        return false;
    }

    Entity entity(command.Handle, command.TargetScene);

    // HandleにはGenerationも含まれるため、同じIndexが再利用されても古いCommandは一致しません。
    // Undo/Redoは過去のEditor操作なので、対象Entityが既に消えている場合は安全に無視します。
    if (command.TargetScene->IsEntityAlive(entity) == false)
    {
        return false;
    }

    if (entity.HasComponent<TransformComponent>() == false)
    {
        return false;
    }

    entity.GetComponent<TransformComponent>() = transform;
    return true;
}

void TrimUndoHistory()
{
    if (s_UndoStack.size() <= MaxHistoryCount)
    {
        return;
    }

    const std::size_t removeCount =
        s_UndoStack.size() - MaxHistoryCount;

    s_UndoStack.erase(
        s_UndoStack.begin(),
        s_UndoStack.begin() + static_cast<std::ptrdiff_t>(removeCount));
}

} // namespace

void RecordEditorTransformCommand(
    Entity entity,
    const TransformComponent& before,
    const TransformComponent& after)
{
    if (static_cast<bool>(entity) == false
        || entity.GetScene() == nullptr)
    {
        return;
    }

    if (entity.GetScene()->IsEntityAlive(entity) == false)
    {
        return;
    }

    // Mouseを押して離しただけ等、実際のTransform変更がない操作は履歴へ追加しません。
    if (TransformEquals(before, after))
    {
        return;
    }

    TransformCommand command;
    command.Handle = entity.GetHandle();
    command.TargetScene = entity.GetScene();
    command.Before = before;
    command.After = after;

    s_UndoStack.push_back(command);
    TrimUndoHistory();

    // Undo後に新しい編集を行った場合、そこから先のRedo分岐は成立しなくなります。
    // 一般的なEditorと同じく、新規Command登録時にRedo履歴を破棄します。
    s_RedoStack.clear();
}

bool UndoEditorCommand()
{
    while (s_UndoStack.empty() == false)
    {
        const TransformCommand command = s_UndoStack.back();
        s_UndoStack.pop_back();

        if (TryApplyTransform(command, command.Before))
        {
            s_RedoStack.push_back(command);
            return true;
        }

        // Entityが既に破棄されていたCommandは履歴から捨て、さらに古い有効Commandを探します。
    }

    return false;
}

bool RedoEditorCommand()
{
    while (s_RedoStack.empty() == false)
    {
        const TransformCommand command = s_RedoStack.back();
        s_RedoStack.pop_back();

        if (TryApplyTransform(command, command.After))
        {
            s_UndoStack.push_back(command);
            TrimUndoHistory();
            return true;
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
}

} // namespace Raven
