#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Diagnostics/DiagnosticAPI.h"
#include "Raven/Diagnostics/EventBuffer.h"

#include <cassert>
#include <cstdint>
#include <array>
#include <atomic>
#include <iostream>
#include <set>
#include <thread>
#include <sstream>
#include <string>
#include <variant>

namespace Raven::tests
{
void RunDiagnosticSelfTests()
{
    using namespace Diagnostics;
    static_assert(EventIds::LegacyRHIInitializationFailed != EventIds::PhysicsGravityUpdated);
    static_assert(EventIds::LegacyRHIInitializationFailed != 0);
    static_assert(EventIds::PhysicsGravityUpdated != 0);

    DiagnosticEvent event;
    event.Id = EventIds::LegacyRHIInitializationFailed;
    event.Category = DiagnosticCategory::RHI;
    event.Severity = DiagnosticSeverity::Error;
    event.TimestampNanoseconds = 123;
    event.ThreadId = 42;
    event.Sequence = 7;
    event.Payload = std::string("backend unavailable");

    std::ostringstream output;
    WriteConsoleEvent(output, event);
    assert(output.str() ==
        "[Diagnostics][ERROR][RHI] id=4097 seq=7 ts_ns=123 thread=42 payload=backend unavailable\n");

    event.Id = EventIds::PhysicsGravityUpdated;
    event.Category = DiagnosticCategory::Physics;
    event.Severity = DiagnosticSeverity::Info;
    event.Payload = std::int64_t(-5);
    output.str("");
    output.clear();
    WriteConsoleEvent(output, event);
    assert(output.str().find("[INFO][Physics] id=8193") != std::string::npos);
    assert(output.str().find("payload=-5\n") != std::string::npos);

    event.Payload = true;
    output.str("");
    output.clear();
    WriteConsoleEvent(output, event);
    assert(output.str().find("payload=1\n") != std::string::npos);

    event.Payload = 1.5;
    output.str("");
    output.clear();
    WriteConsoleEvent(output, event);
    assert(output.str().find("payload=1.5\n") != std::string::npos);

    // UTF-8の2/3/4バイト文字を1024バイト境界で分断しないことを確認します。
    DiagnosticPayload payload = std::string(MaxDiagnosticTextBytes - 1, 'a') + "\xC3\xA9";
    NormalizePayload(payload);
    assert(std::get<std::string>(payload).size() == MaxDiagnosticTextBytes - 1);
    payload = std::string(MaxDiagnosticTextBytes - 2, 'a') + "\xE3\x81\x82";
    NormalizePayload(payload);
    assert(std::get<std::string>(payload).size() == MaxDiagnosticTextBytes - 2);
    payload = std::string(MaxDiagnosticTextBytes - 3, 'a') + "\xF0\x9F\x98\x80";
    NormalizePayload(payload);
    assert(std::get<std::string>(payload).size() == MaxDiagnosticTextBytes - 3);
    payload = std::string(MaxDiagnosticTextBytes, 'a');
    NormalizePayload(payload);
    assert(std::get<std::string>(payload).size() == MaxDiagnosticTextBytes);

    // 同期Console Sinkの行排他・連番の一意性を並行発行で確認します。
    // テスト中だけstdoutを差し替え、すべてのworker終了後に元へ戻します。
    constexpr std::size_t WorkerCount = 4;
    constexpr std::size_t EventsPerWorker = 16;
    std::ostringstream concurrentOutput;
    std::streambuf* previousBuffer = std::cout.rdbuf(concurrentOutput.rdbuf());
    std::array<std::thread, WorkerCount> workers;
    for (std::size_t workerIndex = 0; workerIndex < WorkerCount; ++workerIndex)
    {
        workers[workerIndex] = std::thread([]()
        {
            for (std::size_t eventIndex = 0; eventIndex < EventsPerWorker; ++eventIndex)
            {
                RAVEN_LOG_INFO(DiagnosticCategory::Core, 0x3001u, std::int64_t(1));
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    std::cout.rdbuf(previousBuffer);

    std::istringstream lines(concurrentOutput.str());
    std::set<std::uint64_t> sequences;
    std::string line;
    std::size_t lineCount = 0;
    while (std::getline(lines, line))
    {
        assert(line.find("[Diagnostics][INFO][Core] id=12289 ") == 0);
        assert(line.find(" payload=1") != std::string::npos);
        const std::size_t sequenceStart = line.find(" seq=");
        const std::size_t sequenceEnd = line.find(" ts_ns=");
        assert(sequenceStart != std::string::npos);
        assert(sequenceEnd != std::string::npos);
        sequences.insert(std::stoull(line.substr(sequenceStart + 5,
            sequenceEnd - (sequenceStart + 5))));
        ++lineCount;
    }
    assert(lineCount == WorkerCount * EventsPerWorker);
    assert(sequences.size() == lineCount);
    // Overflowでは既存イベントを上書きせず、Drop数が観測可能であることを確認します。
    EventBuffer overflowBuffer;
    for (std::size_t index = 0; index < EventBuffer::Capacity; ++index)
    {
        DiagnosticEvent item;
        item.Sequence = index + 1;
        item.Payload = std::int64_t(index);
        assert(overflowBuffer.TryPush(std::move(item)) == true);
    }
    assert(overflowBuffer.TryPush(DiagnosticEvent{}) == false);
    assert(overflowBuffer.GetDroppedCount() == 1);
    DiagnosticEvent popped;
    for (std::size_t index = 0; index < EventBuffer::Capacity; ++index)
    {
        assert(overflowBuffer.TryPop(popped) == true);
        assert(popped.Sequence == index + 1);
        assert(std::get<std::int64_t>(popped.Payload) == static_cast<std::int64_t>(index));
    }
    assert(overflowBuffer.TryPop(popped) == false);

    // ProducerとConsumerが同時に動作しても、FIFO順とPayload所有権を維持します。
    EventBuffer concurrentBuffer;
    constexpr std::uint64_t EventCount = 4096;
    std::atomic<bool> producerFinished{ false };
    std::thread producer([&concurrentBuffer, &producerFinished]()
    {
        for (std::uint64_t index = 1; index <= EventCount; ++index)
        {
            DiagnosticEvent item;
            item.Sequence = index;
            item.Payload = std::string("event-") + std::to_string(index);
            while (concurrentBuffer.TryPush(item) == false)
            {
                // 容量超過時はDropを記録する仕様。テストでは同じイベントを再試行します。
                std::this_thread::yield();
            }
        }
        producerFinished.store(true, std::memory_order_release);
    });
    std::uint64_t received = 0;
    while (received < EventCount)
    {
        if (concurrentBuffer.TryPop(popped) == true)
        {
            ++received;
            assert(popped.Sequence == received);
            assert(std::get<std::string>(popped.Payload) ==
                std::string("event-") + std::to_string(received));
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();
    assert(producerFinished.load(std::memory_order_acquire) == true);

}
}
