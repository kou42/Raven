#pragma once

namespace Raven
{

class Scene;

// Editor全体のUndo / Redo対象を型に依存せず同じ履歴へ格納するための共通Interfaceです。
// Historyがunique_ptrでCommandの寿命を単独所有し、CommandはSceneを所有しません。
class IEditorCommand
{
public:
    virtual ~IEditorCommand() = default;

    // 実行前または「実行済みCommand」の登録前に、対象Scene・対象Object・変更内容を検証します。
    virtual bool CanExecute(const Scene& scene) const = 0;
    virtual bool Execute(Scene& scene) = 0;
    virtual bool Undo(Scene& scene) = 0;
    virtual bool Redo(Scene& scene) = 0;
};

} // namespace Raven
