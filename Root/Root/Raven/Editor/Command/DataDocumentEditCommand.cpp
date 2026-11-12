#include "Raven/Editor/Command/DataDocumentEditCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <stdexcept>

namespace Raven
{
DataDocumentEditCommand::DataDocumentEditCommand(const Scene& scene,
    const std::shared_ptr<Data::MutableDataDocument>& target)
    : m_Scene(&scene), m_Target(target), m_HistoryEpoch(GetEditorCommandHistoryEpoch()) {}
std::unique_ptr<DataDocumentEditCommand> DataDocumentEditCommand::Create(const Scene& scene,
    const std::shared_ptr<Data::MutableDataDocument>& target, Data::DataElement edited, Data::DataError* error)
{
    if (error != nullptr) { *error = {}; }
    if (target == nullptr)
    {
        if (error != nullptr) { *error = {Data::DataErrorCode::InvalidStructure, 0}; }
        return nullptr;
    }
    try
    {
        auto command = std::unique_ptr<DataDocumentEditCommand>(new DataDocumentEditCommand(scene, target));
        Data::MutableDataDocument candidate(target->GetLimits());
        if (target->Freeze(command->m_Before, error) == false || candidate.Reset(edited, error) == false
            || candidate.Freeze(command->m_After, error) == false
            || Data::MessagePackWriter::Write(command->m_Before, command->m_BeforeBytes, error) == false
            || Data::MessagePackWriter::Write(command->m_After, command->m_AfterBytes, error) == false)
        { return nullptr; }
        if (command->m_BeforeBytes == command->m_AfterBytes)
        {
            // 意味が変わらないSnapshotでUndo履歴を消費しません。
            if (error != nullptr) { *error = {Data::DataErrorCode::InvalidStructure, 0}; }
            return nullptr;
        }
        return command;
    }
    catch (const std::bad_alloc&)
    {
        if (error != nullptr) { *error = {Data::DataErrorCode::OutOfMemory, 0}; }
        return nullptr;
    }
    catch (const std::length_error&)
    {
        if (error != nullptr) { *error = {Data::DataErrorCode::LimitExceeded, 0}; }
        return nullptr;
    }
}
bool DataDocumentEditCommand::Matches(const Scene& scene) const
{
    m_RetryableFailure = false;
    if (&scene != m_Scene || m_HistoryEpoch == 0 || m_HistoryEpoch != GetEditorCommandHistoryEpoch()) { return false; }
    const auto target = m_Target.lock();
    if (target == nullptr) { return false; }
    try
    {
        Data::DataDocument current;
        std::string bytes;
        // Revision一致だけでは、後続CommandをUndoした後に以前のCommandをUndoできません。
        // 現在値を決定的表現で照合し、履歴外の変更をSnapshotで黙って上書きしないようにします。
        // Object順序と非負整数の符号分類はMessagePackの既存の意味的同値規約に従います。
        Data::DataError error;
        if (target->Freeze(current, &error) == false || Data::MessagePackWriter::Write(current, bytes, &error) == false)
        {
            m_RetryableFailure = error.Code == Data::DataErrorCode::OutOfMemory;
            return false;
        }
        return bytes == (m_Applied ? m_AfterBytes : m_BeforeBytes);
    }
    catch (const std::bad_alloc&) { m_RetryableFailure = true; return false; }
    catch (const std::length_error&) { return false; }
}
bool DataDocumentEditCommand::CanExecute(const Scene& scene) const { return m_Applied == false && Matches(scene); }
bool DataDocumentEditCommand::Apply(Scene& scene, bool applied)
{
    m_RetryableFailure = false;
    if (m_Applied == applied || Matches(scene) == false) { return false; }
    const auto target = m_Target.lock();
    Data::DataError error;
    if (target == nullptr) { return false; }
    if (target->Reset(applied ? m_After.GetRoot() : m_Before.GetRoot(), &error) == false)
    {
        m_RetryableFailure = error.Code == Data::DataErrorCode::OutOfMemory;
        return false;
    }
    // Resetは新Epochへ移すため、Undoで削除値が戻っても削除前Handleは復活しません。
    m_Applied = applied;
    return true;
}
bool DataDocumentEditCommand::Execute(Scene& scene) { return Apply(scene, true); }
bool DataDocumentEditCommand::Undo(Scene& scene) { return Apply(scene, false); }
bool DataDocumentEditCommand::Redo(Scene& scene) { return Apply(scene, true); }
bool DataDocumentEditCommand::RetainAfterFailure() const { return m_RetryableFailure; }
}
