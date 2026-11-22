#pragma once
#include "Raven/Diagnostics/DiagnosticSink.h"
#ifdef _WIN32
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Raven::Diagnostics
{

struct RemoteSinkOptions
{
    std::uint16_t Port = 0;
    std::size_t MaxQueuedEvents = 2048;
    // Binary符号化byteの予算。Queue node/string capacityとWorkerの最大1件は別途有界です。
    std::size_t MaxQueuedBytes = 2u * 1024u * 1024u;
};

struct RemoteSinkStatistics
{
    uint64_t Enqueued = 0;
    uint64_t Dropped = 0;
    uint64_t Sent = 0;
    uint64_t Connections = 0;
    uint64_t ConnectionFailures = 0;
    uint64_t Disconnects = 0;
    std::size_t QueuedEvents = 0;
    std::size_t QueuedBytes = 0;
    int LastError = 0;
    int NativeError = 0;
};

// CollectorのWrite/FlushはSocketを待ちません。Socket生成・破棄は専用Workerに閉じます。
class NetworkDiagnosticSink final : public IDiagnosticSink
{
public:

    explicit NetworkDiagnosticSink(RemoteSinkOptions options);
    ~NetworkDiagnosticSink() override;
    bool Write(const DiagnosticEvent& event) override;
    bool Flush() override;
    void Stop();
    RemoteSinkStatistics GetStatistics() const;

private:

    void Run() noexcept;
    RemoteSinkOptions m_Options;

    struct QueuedEvent
    {
        std::uint64_t Ordinal;
        std::string Bytes;
    };

    mutable std::mutex m_Mutex;

    std::condition_variable m_Wake;

    std::deque<QueuedEvent> m_Queue;

    std::size_t m_QueuedBytes = 0;

    std::thread m_Worker;

    std::atomic<bool> m_Stop{ false };
    std::atomic<bool> m_Fatal{false};

    std::atomic<std::uint64_t> m_Ordinal{ 0 };
    std::atomic<std::uint64_t> m_Enqueued{ 0 };
    std::atomic<std::uint64_t> m_Dropped{ 0 };
    std::atomic<std::uint64_t> m_Sent{ 0 };
    std::atomic<std::uint64_t> m_Connections{ 0 };
    std::atomic<std::uint64_t> m_ConnectionFailures{ 0 };
    std::atomic<std::uint64_t> m_Disconnects{ 0 };

    std::atomic<int> m_LastError{0}, m_NativeError{0};

}; // end NetworkDiagnosticSink

// 受信/試験送信のHeadlessモードを全引数から判定し、該当時だけ処理して終了コードを返します。
bool TryRunRemoteDiagnostics(int argc, char** argv, int& result);

} // end Raven::Diagnostics

#endif
