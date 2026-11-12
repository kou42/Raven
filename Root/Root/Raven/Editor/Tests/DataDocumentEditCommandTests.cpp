#include "Raven/Editor/Tests/DataDocumentEditCommandTests.h"
#include "Raven/Editor/Command/DataDocumentEditCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/Json/JsonWriter.h"
#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include <iostream>
#include <new>

namespace Raven { namespace tests {
namespace {
// History側の一時失敗契約を、OSやglobal new置換に依存せず検証します。
class RetryableCommand final : public IEditorCommand
{
public:
    RetryableCommand(int& value, bool& fail) : m_Value(value), m_Fail(fail) {}
    bool CanExecute(const Scene&) const override { return true; }
    bool Execute(Scene&) override { ++m_Value; return true; }
    bool Undo(Scene&) override { if (m_Fail == true) { return false; } --m_Value; return true; }
    bool Redo(Scene&) override { if (m_Fail == true) { return false; } ++m_Value; return true; }
    bool RetainAfterFailure() const override { return m_Fail; }
private:
    int& m_Value;
    bool& m_Fail;
};
int checks = 0, failures = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (condition == false) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::string Text(const Data::MutableDataDocument& edit)
{
    Data::DataDocument document;
    std::string output;
    Check(edit.Freeze(document) && Data::JsonWriter::Write(document, output), "read edited state");
    return output;
}
}
int RunDataDocumentEditCommandTests()
{
    checks = 0; failures = 0;
    Scene scene, other;
    SetEditorCommandHistoryScene(&scene);
    int retryValue = 0;
    bool retryFailure = false;
    Check(ExecuteAndRecordEditorCommand(std::make_unique<RetryableCommand>(retryValue, retryFailure)), "retryable history entry");
    retryFailure = true;
    Check(UndoEditorCommand() == false && retryValue == 1 && CanUndoEditorCommand() && CanRedoEditorCommand() == false,
        "transient undo failure retains history and state");
    retryFailure = false;
    Check(UndoEditorCommand() && retryValue == 0, "retry undo succeeds");
    retryFailure = true;
    Check(RedoEditorCommand() == false && retryValue == 0 && CanRedoEditorCommand() && CanUndoEditorCommand() == false,
        "transient redo failure retains history and state");
    retryFailure = false;
    Check(RedoEditorCommand() && retryValue == 1, "retry redo succeeds");
    ClearEditorCommandHistory();
    Data::DataDocument original, after, last;
    Check(Data::JsonParser::Parse("{\"a\":[1,2],\"label\":\"before\"}", original)
        && Data::JsonParser::Parse("{\"a\":[3],\"label\":\"after\"}", after)
        && Data::JsonParser::Parse("{\"label\":\"last\"}", last), "command fixtures");
    auto target = std::make_shared<Data::MutableDataDocument>();
    Check(target->Reset(original.GetRoot()), "command target");
    Data::DataError createError;
    Check(DataDocumentEditCommand::Create(scene, target, original.GetRoot(), &createError) == nullptr,
        "no-op command not recorded");
    Check(DataDocumentEditCommand::Create(scene, {}, after.GetRoot(), &createError) == nullptr
        && createError.Code == Data::DataErrorCode::InvalidStructure, "null session rejected");
    Check(DataDocumentEditCommand::Create(scene, target, {}, &createError) == nullptr,
        "invalid edited snapshot rejected");
    const auto beforeText = Text(*target);
    const auto oldRoot = target->GetRoot();
    Data::MutableDataHandle deleted;
    Check(target->TryFind(oldRoot, "a", deleted), "deleted target handle");
    auto command = DataDocumentEditCommand::Create(scene, target, after.GetRoot());
    Check(command != nullptr && command->CanExecute(other) == false && command->Execute(other) == false, "wrong scene rejected");
    Check(ExecuteAndRecordEditorCommand(std::move(command)) && CanUndoEditorCommand(), "execute through existing history");
    const auto afterText = Text(*target);
    const auto executedRoot = target->GetRoot();
    Check(target->IsValid(oldRoot) == false && target->IsValid(deleted) == false, "execute invalidates previous handles");
    Check(UndoEditorCommand() && Text(*target) == beforeText && target->IsValid(oldRoot) == false
        && target->IsValid(executedRoot) == false, "undo restores value without old handles");
    Check(RedoEditorCommand() && Text(*target) == afterText, "redo restores edited value");
    Check(ExecuteAndRecordEditorCommand(DataDocumentEditCommand::Create(scene, target, last.GetRoot())), "second edit");
    const auto lastText = Text(*target);
    Check(UndoEditorCommand() && Text(*target) == afterText && UndoEditorCommand()
        && Text(*target) == beforeText, "two commands undo across revision changes");
    Check(RedoEditorCommand() && Text(*target) == afterText && RedoEditorCommand()
        && Text(*target) == lastText, "two commands redo");
    Check(UndoEditorCommand(), "prepare redo branch");
    Check(ExecuteAndRecordEditorCommand(DataDocumentEditCommand::Create(scene, target, original.GetRoot()))
        && CanRedoEditorCommand() == false, "new edit clears redo branch");
    auto stale = DataDocumentEditCommand::Create(scene, target, last.GetRoot());
    Check(target->Reset(after.GetRoot()), "external edit");
    Check(stale != nullptr && stale->CanExecute(scene) == false && stale->Execute(scene) == false
        && Text(*target) == afterText, "stale command cannot overwrite external edit");
    ClearEditorCommandHistory();
    Check(target->Reset(original.GetRoot()), "prepare expired session");
    auto expiring = DataDocumentEditCommand::Create(scene, target, after.GetRoot());
    target.reset();
    Check(expiring != nullptr && expiring->CanExecute(scene) == false && expiring->Execute(scene) == false, "weak session expires safely");
    target = std::make_shared<Data::MutableDataDocument>();
    Check(target->Reset(original.GetRoot()), "prepare scene switch");
    auto detached = DataDocumentEditCommand::Create(scene, target, after.GetRoot());
    Check(ExecuteAndRecordEditorCommand(DataDocumentEditCommand::Create(scene, target, after.GetRoot())), "scene bound history entry");
    SetEditorCommandHistoryScene(&other);
    Check(CanUndoEditorCommand() == false && CanRedoEditorCommand() == false
        && detached->CanExecute(scene) == false, "scene switch clears history and invalidates detached command");
    SetEditorCommandHistoryScene(&scene);
    Check(target->Reset(original.GetRoot()), "prepare direct undo mismatch");
    command = DataDocumentEditCommand::Create(scene, target, after.GetRoot());
    Check(command != nullptr && command->Execute(scene) && target->Reset(last.GetRoot()), "external edit after execute");
    Check(command->Undo(scene) == false && Text(*target) == lastText, "undo refuses external change");
    // Sceneアドレスの再利用を固定し、履歴破棄前通知とEpochの組み合わせを検証します。
    alignas(Scene) std::byte sceneStorage[sizeof(Scene)];
    Scene* reused = new (sceneStorage) Scene();
    SetEditorCommandHistoryScene(reused);
    Check(target->Reset(original.GetRoot()), "prepare reused scene");
    auto oldSceneCommand = DataDocumentEditCommand::Create(*reused, target, after.GetRoot());
    SetEditorCommandHistoryScene(nullptr);
    reused->~Scene();
    reused = new (sceneStorage) Scene();
    SetEditorCommandHistoryScene(reused);
    Check(oldSceneCommand->CanExecute(*reused) == false, "same address new scene rejected");
    SetEditorCommandHistoryScene(nullptr);
    reused->~Scene();
    // Undo/Redoした専用AssetをFreeze→既存の保存処理へ渡す実経路も検証します。
    SetEditorCommandHistoryScene(&scene);
    Check(Data::JsonParser::Parse("{\"type\":\"Raven.StructuredDataTest\",\"version\":2,\"enabled\":true,\"label\":\"before\",\"values\":[1,2]}", original)
        && target->Reset(original.GetRoot()), "asset original");
    Data::MutableDataDocument draft;
    Data::MutableDataHandle label, replaced;
    Check(draft.Reset(original.GetRoot()) && draft.TryFind(draft.GetRoot(), "label", label)
        && Data::JsonParser::Parse("\"edited\"", last) && draft.Replace(label, last.GetRoot(), replaced)
        && draft.Freeze(after), "prepare edited asset snapshot");
    Check(ExecuteAndRecordEditorCommand(DataDocumentEditCommand::Create(scene, target, after.GetRoot()))
        && UndoEditorCommand() && RedoEditorCommand() && target->Freeze(after), "asset edit undo redo freeze");
    Data::Tests::TestSettings settings, restored;
    Check(Data::Deserialize(after.GetRoot(), settings), "edited asset type conversion");
    std::filesystem::path directory;
    for (unsigned i = 0; i < 128; ++i)
    {
        auto candidate = std::filesystem::temp_directory_path() / ("raven-editor-data-" + std::to_string(i));
        if (std::filesystem::create_directory(candidate)) { directory = candidate; break; }
    }
    Check(directory.empty() == false, "isolated asset directory");
    if (directory.empty() == false)
    {
        const auto path = directory / "edited.json";
        Data::Tests::TestAssetError error;
        Check(Data::Tests::SaveTestAsset(path, settings, error) && Data::Tests::LoadTestAsset(path, restored, error)
            && restored.Label == "edited" && restored.Values == std::vector<int>({1,2}), "editor history to disk round trip");
        std::filesystem::remove(path); std::filesystem::remove(directory);
    }
    SetEditorCommandHistoryScene(nullptr);
    std::cout << "DataDocumentEditCommand: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
} }
