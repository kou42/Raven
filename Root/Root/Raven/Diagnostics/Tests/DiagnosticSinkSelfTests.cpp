#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Diagnostics/BinaryTrace.h"
#include "Raven/Diagnostics/DiagnosticCollector.h"

#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <set>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Raven::tests
{
namespace
{
using namespace Diagnostics;

void Require(bool condition, std::source_location location = std::source_location::current())
{
    if (condition == false)
    {
        throw std::runtime_error(std::string(location.file_name()) + ":" + std::to_string(location.line()));
    }
}

class TemporaryFiles
{
public:
    TemporaryFiles()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        Directory = std::filesystem::temp_directory_path() / ("raven-diagnostics-" + std::to_string(suffix));
        Require(std::filesystem::create_directory(Directory));
    }
    ~TemporaryFiles()
    {
        // このテストが作った既知のファイルだけを削除し、再帰削除は行いません。
        std::error_code error;
        std::filesystem::remove(Directory / "events.log", error);
        std::filesystem::remove(Directory / "events.rvndiag", error);
        std::filesystem::remove(Directory, error);
    }
    std::filesystem::path Directory;
};

// Write失敗と例外、終了Flush失敗をGPUやOS依存の故障注入なしで再現します。
class FailingSink final : public IDiagnosticSink
{
public:
    explicit FailingSink(bool throwOnWrite, bool failOnFlush)
        : m_Throw(throwOnWrite), m_Flush(failOnFlush)
    {
    }
    bool Write(const DiagnosticEvent&) override
    {
        if (m_Throw == true)
        {
            throw std::runtime_error("injected write failure");
        }
        return m_Flush;
    }
    bool Flush() override
    {
        return false;
    }
private:
    bool m_Throw;
    bool m_Flush;
};

class FailingOutputBuffer final : public std::streambuf
{
    std::streamsize xsputn(const char*, std::streamsize) override
    {
        return 0;
    }
};

void TestBinaryFormat()
{
    DiagnosticEvent sample;
    sample.Id = 0x123456789abcdef0ull;
    sample.TimestampNanoseconds = 0x1020304050607080ull;
    sample.ThreadId = 0x8899aabbccddeeffull;
    sample.Sequence = 0xfedcba9876543210ull;
    sample.Category = DiagnosticCategory::Physics;
    sample.Severity = DiagnosticSeverity::Warning;
    const std::array<DiagnosticPayload, 7> payloads{
        std::numeric_limits<std::int64_t>::min(), -1.25, true, false,
        std::string("日本語\0event", 15), std::string(), std::string(MaxDiagnosticTextBytes, 'z') };
    std::ostringstream saved(std::ios::binary);
    Require(WriteBinaryTraceHeader(saved));
    for (const auto& payload : payloads)
    {
        sample.Payload = payload;
        Require(WriteBinaryTraceEvent(saved, sample));
    }
    const auto bytes = saved.str();
    // 独立した期待byteでendianness・version・型tag・signed整数の表現を固定します。
    const std::array<unsigned char, 16> header{ 'R', 'V', 'N', 'D', 'I', 'A', 'G', 0, 1, 0, 0, 0, 0, 0, 0, 0 };
    for (std::size_t index = 0; index < header.size(); ++index)
    {
        Require(static_cast<unsigned char>(bytes[index]) == header[index]);
    }
    Require(static_cast<unsigned char>(bytes[16]) == 48);
    Require(static_cast<unsigned char>(bytes[20]) == 0xf0);
    Require(static_cast<unsigned char>(bytes[27]) == 0x12);
    Require(bytes[52] == 3 && bytes[53] == 1 && bytes[54] == 1 && bytes[56] == 8);
    for (std::size_t index = 60; index < 67; ++index)
    {
        Require(bytes[index] == 0);
    }
    Require(static_cast<unsigned char>(bytes[67]) == 0x80);

    std::istringstream input(bytes, std::ios::binary);
    BinaryTraceReader reader(input);
    DiagnosticEvent decoded;
    for (const auto& payload : payloads)
    {
        Require(reader.ReadNext(decoded) == TraceReadResult::Event);
        Require(decoded.Id == sample.Id && decoded.Category == sample.Category && decoded.Severity == sample.Severity);
        Require(decoded.TimestampNanoseconds == sample.TimestampNanoseconds);
        Require(decoded.ThreadId == sample.ThreadId && decoded.Sequence == sample.Sequence);
        Require(decoded.Payload == payload);
    }
    Require(reader.ReadNext(decoded) == TraceReadResult::EndOfStream);
    Require(reader.ReadNext(decoded) == TraceReadResult::EndOfStream);

    // 1 recordの全byte位置で途中切断し、正常EOFと壊れた入力を区別します。
    std::ostringstream single;
    Require(WriteBinaryTraceHeader(single));
    sample.Payload = true;
    Require(WriteBinaryTraceEvent(single, sample));
    const auto valid = single.str();
    for (std::size_t length = 0; length < valid.size(); ++length)
    {
        std::istringstream truncated(valid.substr(0, length));
        BinaryTraceReader truncatedReader(truncated);
        decoded.Id = 77;
        const auto result = truncatedReader.ReadNext(decoded);
        Require(result == (length == 16 ? TraceReadResult::EndOfStream : TraceReadResult::InvalidData));
        Require(decoded.Id == 77);
    }

    const auto reject = [&valid](std::size_t offset, unsigned char value, TraceReadResult expected)
    {
        auto corrupted = valid;
        corrupted[offset] = static_cast<char>(value);
        std::istringstream stream(corrupted);
        BinaryTraceReader corruptedReader(stream);
        DiagnosticEvent untouched;
        untouched.Id = 77;
        Require(corruptedReader.ReadNext(untouched) == expected);
        Require(corruptedReader.ReadNext(untouched) == expected);
        Require(untouched.Id == 77);
    };
    reject(0, 'X', TraceReadResult::InvalidData);
    reject(8, 2, TraceReadResult::UnsupportedVersion);
    reject(10, 1, TraceReadResult::UnsupportedVersion);
    reject(12, 1, TraceReadResult::InvalidData);
    reject(16, 0, TraceReadResult::InvalidData);
    reject(19, 0xff, TraceReadResult::InvalidData); // 巨大sizeを確保前に拒否
    reject(52, 255, TraceReadResult::InvalidData);
    reject(53, 255, TraceReadResult::InvalidData);
    reject(54, 255, TraceReadResult::InvalidData);
    reject(54, 1, TraceReadResult::InvalidData); // 整数なのに1byte
    reject(55, 1, TraceReadResult::InvalidData);
    reject(56, 2, TraceReadResult::InvalidData);
    reject(60, 2, TraceReadResult::InvalidData); // boolは0/1のみ

    std::istringstream broken;
    broken.setstate(std::ios::badbit);
    BinaryTraceReader brokenReader(broken);
    Require(brokenReader.ReadNext(decoded) == TraceReadResult::IOError);
    std::istringstream throwingInput(valid.substr(0, valid.size() - 1));
    throwingInput.exceptions(std::ios::failbit | std::ios::badbit);
    BinaryTraceReader throwingReader(throwingInput);
    Require(throwingReader.ReadNext(decoded) == TraceReadResult::InvalidData);
    std::ostringstream invalidOutput;
    sample.Id = 0;
    Require(WriteBinaryTraceEvent(invalidOutput, sample) == false);
    sample.Id = 1;
    sample.Payload = std::string(MaxDiagnosticTextBytes + 1, 'x');
    Require(WriteBinaryTraceEvent(invalidOutput, sample) == false);
    Require(invalidOutput.str().empty());
    FailingOutputBuffer failedBuffer;
    std::ostream failedOutput(&failedBuffer);
    Require(WriteBinaryTraceHeader(failedOutput) == false);
}

void TestParallelTrace()
{
    TemporaryFiles files;
    const auto path = files.Directory / "events.rvndiag";
    const auto droppedBefore = GetAsyncDroppedCount();
    constexpr std::size_t Workers = 4;
    constexpr std::int64_t PerWorker = 2048;
    std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
    sinks.push_back(std::make_unique<BinaryTraceSink>(path));
    {
        DiagnosticSession session(std::move(sinks));
        Require(session.IsActive());
        std::array<std::thread, Workers> workers;
        for (std::size_t worker = 0; worker < Workers; ++worker)
        {
            workers[worker] = std::thread([worker]()
            {
                for (std::int64_t index = 0; index < PerWorker; ++index)
                {
                    RAVEN_LOG_INFO(DiagnosticCategory::Physics, 0x8001u + worker, index);
                }
            });
        }
        for (auto& worker : workers)
        {
            worker.join();
        }
    }
    std::ifstream input(path, std::ios::binary);
    BinaryTraceReader reader(input);
    DiagnosticEvent event;
    std::set<std::uint64_t> sequences;
    std::array<std::uint64_t, Workers> threadIds{};
    std::array<std::int64_t, Workers> previous{ -1, -1, -1, -1 };
    std::size_t received = 0;
    TraceReadResult result;
    while ((result = reader.ReadNext(event)) == TraceReadResult::Event)
    {
        Require(event.Id >= 0x8001u && event.Id < 0x8001u + Workers);
        const auto worker = static_cast<std::size_t>(event.Id - 0x8001u);
        const auto value = std::get<std::int64_t>(event.Payload);
        Require(value > previous[worker] && value < PerWorker);
        Require(event.TimestampNanoseconds > 0 && event.ThreadId > 0);
        if (threadIds[worker] == 0)
        {
            threadIds[worker] = event.ThreadId;
        }
        Require(threadIds[worker] == event.ThreadId);
        previous[worker] = value;
        Require(sequences.insert(event.Sequence).second);
        ++received;
    }
    Require(result == TraceReadResult::EndOfStream);
    Require(received > 0);
    Require(received + GetAsyncDroppedCount() - droppedBefore == Workers * PerWorker);
    std::set<std::uint64_t> uniqueThreads(threadIds.begin(), threadIds.end());
    Require(uniqueThreads.size() == Workers && uniqueThreads.count(0) == 0);
}

void TestSinksAndLifecycle()
{
    TemporaryFiles files;
    const auto textPath = files.Directory / "events.log";
    const auto tracePath = files.Directory / "events.rvndiag";
    std::ostringstream console;
    DiagnosticFilter warnings;
    warnings.MinimumSeverity = DiagnosticSeverity::Warning;
    warnings.CategoryMask = 1u << static_cast<unsigned>(DiagnosticCategory::Physics);
    std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
    sinks.push_back(std::make_unique<ConsoleSink>(console, warnings));
    auto fileSink = std::make_unique<FileSink>(textPath);
    auto traceSink = std::make_unique<BinaryTraceSink>(tracePath);
    Require(fileSink->IsOpen() && traceSink->IsOpen());
    sinks.push_back(std::move(fileSink));
    sinks.push_back(std::move(traceSink));
    {
        DiagnosticSession session(std::move(sinks));
        Require(session.IsActive());
        { DiagnosticSession nested; Require(nested.IsActive() == false); }
        RAVEN_LOG_INFO(DiagnosticCategory::RHI, 0x7001u, std::string("created"));
        RAVEN_LOG_WARN(DiagnosticCategory::Physics, 0x7002u, -3.5);
        RAVEN_LOG_ERROR(DiagnosticCategory::RHI, 0x7003u, true);
        // Flush完了前に呼出側の文字列を破棄しても、Sinkへ所有Payloadを渡せます。
        {
            std::string owned = "temporary payload";
            RAVEN_LOG_INFO(DiagnosticCategory::Core, 0x7004u, owned);
            owned.assign("changed");
        }
        FlushDiagnostics();
        Require(console.str().find("id=28674") != std::string::npos);
        Require(console.str().find("id=28673") == std::string::npos);
        Require(console.str().find("id=28675") == std::string::npos);
        // Stop/destructorによる最終Flushも確認します。
        RAVEN_LOG_INFO(DiagnosticCategory::Core, 0x7005u, std::int64_t(-9));
    }
    std::ifstream text(textPath, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(text)), {});
    Require(contents.find("payload=temporary payload") != std::string::npos);
    Require(contents.find("payload=-9") != std::string::npos);
    std::ifstream trace(tracePath, std::ios::binary);
    BinaryTraceReader reader(trace);
    DiagnosticEvent event;
    for (std::uint64_t index = 0; index < 5; ++index)
    {
        Require(reader.ReadNext(event) == TraceReadResult::Event);
        Require(event.Id == 0x7001u + index);
        if (index == 1)
        {
            Require(std::get<double>(event.Payload) == -3.5);
        }
        if (index == 3)
        {
            Require(std::get<std::string>(event.Payload) == "temporary payload");
        }
    }
    Require(reader.ReadNext(event) == TraceReadResult::EndOfStream);
    text.close();
    trace.close();

    // 無効なpathのSinkと故障注入Sinkを同時に渡し、健全なSinkの継続を確認します。
    const auto failuresBefore = GetDiagnosticSinkFailureCount();
    std::ostringstream healthy;
    std::vector<std::unique_ptr<IDiagnosticSink>> faulty;
    faulty.push_back(std::make_unique<FileSink>(files.Directory / "missing" / "output.log"));
    faulty.push_back(std::make_unique<BinaryTraceSink>(files.Directory / "missing" / "output.bin"));
    faulty.push_back(std::make_unique<FailingSink>(true, false));
    faulty.push_back(std::make_unique<FailingSink>(false, true));
    faulty.push_back(std::make_unique<ConsoleSink>(healthy));
    {
        DiagnosticSession session(std::move(faulty));
        Require(session.IsActive());
        RAVEN_LOG_ERROR(DiagnosticCategory::RHI, 0x7101u, std::string("still delivered"));
        FlushDiagnostics();
        Require(GetDiagnosticSinkFailureCount() - failuresBefore == 4);
        RAVEN_LOG_INFO(DiagnosticCategory::Physics, 0x7102u, std::int64_t(2));
    }
    Require(GetDiagnosticSinkFailureCount() - failuresBefore == 4);
    Require(healthy.str().find("still delivered") != std::string::npos);
    Require(healthy.str().find("id=28930") != std::string::npos);
    Require(StartAsyncLogging(std::vector<std::unique_ptr<IDiagnosticSink>>{}) == false);

    std::ostringstream recycledOutput;
    DiagnosticCollector collector(recycledOutput);
    auto buffer = std::make_shared<EventBuffer>();
    Require(collector.RegisterBuffer(buffer));
    Require(collector.Start());
    event.Id = 0x7201u;
    event.Payload = std::string("last event before close");
    Require(buffer->TryPush(event));
    buffer->Close();
    Require(buffer->TryPush(event) == false);
    collector.Flush();
    Require(collector.GetCollectedCount() == 1);
    Require(collector.GetRegisteredBufferCount() == 0);
    collector.Stop();
    Require(collector.Start() == false);

    int evaluated = 0;
    SetDiagnosticsEnabled(false);
    RAVEN_LOG_INFO(DiagnosticCategory::Core, 1u, std::int64_t(++evaluated));
    Require(evaluated == 0);
    SetDiagnosticsEnabled(true);
}
}

void RunDiagnosticSinkSelfTests()
{
    TestBinaryFormat();
    TestSinksAndLifecycle();
    TestParallelTrace();
}
}
