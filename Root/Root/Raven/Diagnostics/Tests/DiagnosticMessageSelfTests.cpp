#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Diagnostics/Generated/MessageMetadata.generated.h"
#include "Raven/Diagnostics/BinaryTrace.h"
#include "Raven/Editor/Panels/DiagnosticsViewModel.h"
#include <bit>
#include <limits>
#include <set>
#include <source_location>
#include <sstream>
#include <stdexcept>

namespace Raven::tests
{
namespace
{
void CheckMessage(bool condition, std::source_location where = std::source_location::current())
{
    if (condition == false) { throw std::runtime_error(std::string(where.file_name()) + ':' + std::to_string(where.line())); }
}
std::string Save(const Diagnostics::DiagnosticEvent& event)
{
    std::ostringstream stream(std::ios::binary);
    CheckMessage(Diagnostics::WriteBinaryTraceHeader(stream) && Diagnostics::WriteBinaryTraceEvent(stream, event));
    return stream.str();
}
Diagnostics::TraceReadResult Read(const std::string& bytes, Diagnostics::DiagnosticEvent& event)
{
    std::istringstream input(bytes, std::ios::binary);
    Diagnostics::BinaryTraceReader reader(input);
    return reader.ReadNext(event);
}
}
void RunDiagnosticMessageSelfTests()
{
    using namespace Diagnostics;
    static_assert(Messages::PhysicsGravityUpdated == EventIds::PhysicsGravityUpdated);
    static_assert(Messages::LegacyRHIInitializationFailed == EventIds::LegacyRHIInitializationFailed);
    std::set<DiagnosticEventId> ids;
    for (const auto& definition : GetMessageDefinitions()) { CheckMessage(ids.insert(definition.Id).second); }
    auto gravity = Messages::MakePhysicsGravityUpdated(0, -9.5, 1);
    CheckMessage(gravity.IsValid());
    CheckMessage(FormatDiagnosticMessage(Messages::PhysicsGravityUpdated, gravity).Text == "Physics gravity updated: (0, -9.5, 1)");
    const auto japanese = FormatDiagnosticMessage(Messages::PhysicsGravityUpdated, gravity, DiagnosticLanguage::Japanese);
    CheckMessage(japanese.Status == MessageFormatStatus::Formatted && japanese.Text == "物理の重力を更新しました: (0, -9.5, 1)");
    auto state = Messages::MakeDiagnosticState(-42, true, "{session} UTF-8 日本語");
    CheckMessage(FormatDiagnosticMessage(Messages::DiagnosticState, state).Text.find("label={session} UTF-8 日本語") != std::string::npos);
    DiagnosticEvent event; event.Id = Messages::DiagnosticState; event.Payload = state;
    const auto bytes = Save(event);
    CheckMessage(bytes[10] == 3 && bytes[54] == 7 && bytes[60] == 1 && bytes[62] == 1 && bytes[72] == 3);
    // 独立したwire位置でintegerの符号とbool表現を固定します。
    CheckMessage(bytes[76] == 1 && static_cast<unsigned char>(bytes[80]) == 214 && static_cast<unsigned char>(bytes[87]) == 255);
    CheckMessage(bytes[88] == 3 && bytes[92] == 1 && bytes[93] == 4);
    DiagnosticEvent decoded;
    CheckMessage(Read(bytes, decoded) == TraceReadResult::Event && std::get<DiagnosticMessage>(decoded.Payload) == state);
    CheckMessage(FormatDiagnosticMessage(decoded.Id, std::get<DiagnosticMessage>(decoded.Payload), DiagnosticLanguage::Japanese).Text.find("診断セッション -42") == 0);
    for (const auto& message : {gravity, state, Messages::MakeLegacyRHIInitializationFailed("backend")})
    {
        event.Payload = message;
        CheckMessage(Read(Save(event), decoded) == TraceReadResult::Event && std::get<DiagnosticMessage>(decoded.Payload) == message);
    }
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        const auto result = Read(bytes.substr(0, length), decoded);
        CheckMessage(result == (length == 16 ? TraceReadResult::EndOfStream : TraceReadResult::InvalidData));
    }
    for (const auto offset : {60u, 62u, 64u, 72u, 73u, 74u, 76u, 77u, 78u, 92u})
    {
        auto bad = bytes; bad[offset] = static_cast<char>(255);
        // 未知のDictionary/Schema/hashは構造が正しければ読め、生値へfallbackします。
        const auto result = Read(bad, decoded);
        if (offset == 60 || offset == 62 || offset == 64) { CheckMessage(result == TraceReadResult::Event); }
        else { CheckMessage(result == TraceReadResult::InvalidData); }
    }
    for (int minor = 0; minor <= 2; ++minor)
    {
        auto old = bytes; old[10] = static_cast<char>(minor);
        CheckMessage(Read(old, decoded) == TraceReadResult::InvalidData);
        event.Payload = std::int64_t(-123); old = Save(event); old[10] = static_cast<char>(minor);
        CheckMessage(Read(old, decoded) == TraceReadResult::Event && std::get<std::int64_t>(decoded.Payload) == -123);
    }
    auto mismatch = gravity; ++mismatch.SchemaHash;
    CheckMessage(FormatDiagnosticMessage(Messages::PhysicsGravityUpdated, mismatch).Status == MessageFormatStatus::SchemaMismatch);
    mismatch = gravity; ++mismatch.DictionaryVersion;
    CheckMessage(FormatDiagnosticMessage(Messages::PhysicsGravityUpdated, mismatch).Status == MessageFormatStatus::UnsupportedDictionary);
    CheckMessage(FormatDiagnosticMessage(0xabcdef, gravity).Status == MessageFormatStatus::UnknownMessage);
    mismatch = gravity; mismatch.Arguments[0] = std::int64_t(1);
    CheckMessage(FormatDiagnosticMessage(Messages::PhysicsGravityUpdated, mismatch).Status == MessageFormatStatus::SchemaMismatch);
    mismatch = gravity; mismatch.Arguments[0] = std::numeric_limits<double>::infinity();
    CheckMessage(mismatch.IsValid() == false);
    event.Payload = mismatch; std::ostringstream invalid; CheckMessage(WriteBinaryTraceEvent(invalid, event) == false);
    auto oversized = Messages::MakeLegacyRHIInitializationFailed(std::string(257, 'x'));
    CheckMessage(oversized.IsValid() == false);
    DiagnosticMessage maximum{1, 1, 1, std::vector<DiagnosticArgument>(4, std::string(256, 'x'))};
    CheckMessage(maximum.IsValid()); event.Payload = maximum;
    CheckMessage(Read(Save(event), decoded) == TraceReadResult::Event);
    maximum.Arguments.push_back(true); CheckMessage(maximum.IsValid() == false);
    maximum.Arguments.assign(8, true); CheckMessage(maximum.IsValid());
    maximum.Arguments.push_back(false); CheckMessage(maximum.IsValid() == false);
    maximum.Arguments.clear(); maximum.DictionaryVersion = 0; CheckMessage(maximum.IsValid() == false);

    DiagnosticEventStore ownedStore;
    event.Id = Messages::DiagnosticState; event.Payload = state; ownedStore.Append(event);
    const auto owned = ownedStore.TrySnapshot(0);
    std::get<DiagnosticMessage>(event.Payload).Arguments[2] = std::string("changed");
    ownedStore.Clear();
    CheckMessage(std::get<DiagnosticMessage>(owned->Events.front().Event.Payload) == state);
    DiagnosticEventStore byteBounded({8, sizeof(DiagnosticStoredEvent) * 8 + 700});
    event.Payload = Messages::MakeDiagnosticState(1, true, std::string(256, 'x'));
    byteBounded.Append(event); byteBounded.Append(event);
    const auto bounded = byteBounded.TrySnapshot(0);
    CheckMessage(bounded->Events.size() == 1 && bounded->Evicted == 1);
    CheckMessage(bounded->RetainedBytes <= sizeof(DiagnosticStoredEvent) * 8 + 700);

    DiagnosticEventStore store; event.Id = Messages::PhysicsGravityUpdated; event.Payload = gravity; store.Append(event);
    DiagnosticsViewModel view; view.SetSnapshot(store.TrySnapshot(0)); view.SelectRow(0);
    auto filter = view.GetFilter(); filter.Language = DiagnosticLanguage::Japanese; filter.Search = "重力"; view.SetFilter(filter);
    CheckMessage(view.GetRows().size() == 1 && view.GetSelectedEvent() != nullptr);
    filter.Language = DiagnosticLanguage::English; view.SetFilter(filter);
    CheckMessage(view.GetRows().empty() && view.GetSelectedEvent() != nullptr);
    std::ostringstream console; WriteConsoleEvent(console, event);
    CheckMessage(console.str().find("Physics gravity updated:") != std::string::npos);
    int evaluated = 0; SetDiagnosticCategoryEnabled(DiagnosticCategory::Physics, false);
    RAVEN_MESSAGE(Messages::PhysicsGravityUpdated, Messages::MakePhysicsGravityUpdated(++evaluated, 0, 0));
    SetDiagnosticCategoryEnabled(DiagnosticCategory::Physics, true);
    CheckMessage(evaluated == 0);
    SetDiagnosticsEnabled(false);
    RAVEN_MESSAGE(Messages::PhysicsGravityUpdated, Messages::MakePhysicsGravityUpdated(++evaluated, 0, 0));
    SetDiagnosticsEnabled(true); CheckMessage(evaluated == 0);
    auto collected = std::make_shared<DiagnosticEventStore>();
    {
        std::vector<std::unique_ptr<IDiagnosticSink>> sinks; sinks.push_back(std::make_unique<DiagnosticStoreSink>(collected));
        DiagnosticSession session(std::move(sinks)); CheckMessage(session.IsActive());
        CheckMessage(EmitStructuredMessage(Messages::PhysicsGravityUpdated, gravity));
        CheckMessage(EmitStructuredMessage(Messages::PhysicsGravityUpdated, mismatch) == false);
        FlushDiagnostics();
    }
    CheckMessage(collected->TrySnapshot(0)->Events.size() == 1);
}
}
