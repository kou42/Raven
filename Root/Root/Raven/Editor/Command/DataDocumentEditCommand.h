#pragma once
#include "Raven/Editor/Command/IEditorCommand.h"
#include "Raven/Data/MutableDataDocument.h"
#include <memory>

namespace Raven
{
// Sceneの履歴に編集Sessionを接続する境界。Data層はScene/Historyを知りません。
// Session所有者はshared_ptrを保持し、Commandはweak_ptrだけを保持します。
// Scene交換で履歴を破棄する既存規約を使い、Undo/Redoは所有する不変Snapshotから復元します。
class DataDocumentEditCommand final : public IEditorCommand
{
public:
    static std::unique_ptr<DataDocumentEditCommand> Create(const Scene& scene,
        const std::shared_ptr<Data::MutableDataDocument>& target, Data::DataElement edited,
        Data::DataError* error = nullptr);
    bool CanExecute(const Scene& scene) const override;
    bool Execute(Scene& scene) override;
    bool Undo(Scene& scene) override;
    bool Redo(Scene& scene) override;
    bool RetainAfterFailure() const override;
private:
    DataDocumentEditCommand(const Scene& scene, const std::shared_ptr<Data::MutableDataDocument>& target);
    bool Matches(const Scene& scene) const;
    bool Apply(Scene& scene, bool applied);
    const Scene* m_Scene;
    std::weak_ptr<Data::MutableDataDocument> m_Target;
    Data::DataDocument m_Before;
    Data::DataDocument m_After;
    std::string m_BeforeBytes;
    std::string m_AfterBytes;
    bool m_Applied = false;
    std::uint64_t m_HistoryEpoch;
    mutable bool m_RetryableFailure = false;
};
}
