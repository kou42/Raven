#include "Raven/Diagnostics/RemoteDiagnostics.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "Raven/Diagnostics/RemoteProtocol.h"
#include "Raven/Diagnostics/Generated/MessageMetadata.generated.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"
#include "Raven/Network/Windows/WindowsTcpSocket.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <process.h>
#include <cstdio>

namespace Raven::Diagnostics
{

namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto IOTimeout = std::chrono::milliseconds(100);
constexpr auto StopGrace = std::chrono::milliseconds(250);
constexpr auto RetryDelay = std::chrono::milliseconds(50);
std::atomic<std::uint64_t> NextSession{0};
}

NetworkDiagnosticSink::NetworkDiagnosticSink(RemoteSinkOptions options) : m_Options(options)
{
    if (options.Port == 0
        || options.MaxQueuedEvents == 0
        || options.MaxQueuedEvents > 65536
        || options.MaxQueuedBytes < MaxRemoteEventBytes
        || options.MaxQueuedBytes > 64u * 1024u * 1024u
    ) {
        throw std::invalid_argument("Invalid remote diagnostic limits");
    }
    m_Worker = std::thread(&NetworkDiagnosticSink::Run, this);
}

NetworkDiagnosticSink::~NetworkDiagnosticSink()
{
    Stop();
    const auto stats = GetStatistics();
    char summary[384]{};
    std::snprintf(summary, sizeof(summary), "[RemoteDiagnostics] attempted=%llu enqueued=%llu os_sent=%llu dropped=%llu connections=%llu connect_failures=%llu disconnects=%llu last_error=%d native_error=%d",
        static_cast<unsigned long long>(m_Ordinal.load()), static_cast<unsigned long long>(stats.Enqueued),
        static_cast<unsigned long long>(stats.Sent), static_cast<unsigned long long>(stats.Dropped),
        static_cast<unsigned long long>(stats.Connections), static_cast<unsigned long long>(stats.ConnectionFailures),
        static_cast<unsigned long long>(stats.Disconnects), stats.LastError, stats.NativeError
    );

    // Session制御mutex内から破棄されても再入しない、独立stderr経路だけを使います。
    WriteEmergencyDiagnostic(summary);
}

bool NetworkDiagnosticSink::Write(const DiagnosticEvent& event)
{
    if (m_Fatal.load() == true)
    {
        return false;
    }

    const auto ordinal = m_Ordinal.fetch_add(1) + 1;
    auto bytes = EncodeRemoteEvent(event);

    if (bytes.empty() == true)
    {
        ++m_Dropped; return false;
    }

    // このLockはQueueの更新だけを直列化します。WorkerはI/O・再接続・Counter取得をLock外で行います。
    // 瞬間的なLock競合を欠落に変換せず、満杯/停止時だけ観測可能なDropとします。
    std::unique_lock lock(m_Mutex);

    if (m_Stop.load() == true
        || m_Queue.size() >= m_Options.MaxQueuedEvents
        || m_QueuedBytes + bytes.size() > m_Options.MaxQueuedBytes
    ) {
        ++m_Dropped;
        return true;
    }

    const auto queuedBytes = bytes.size();
    m_Queue.push_back({ordinal, std::move(bytes)});
    // node確保に失敗した場合もbyte会計だけを進めないよう、挿入成功後に反映します。
    m_QueuedBytes += queuedBytes; ++m_Enqueued;
    lock.unlock();
    m_Wake.notify_one();
    return true;
}

bool NetworkDiagnosticSink::Flush()
{
    m_Wake.notify_one();
    return m_Fatal.load() == false;
}

void NetworkDiagnosticSink::Stop()
{
    // 呼出側はCollectorをFlushし、Writeの発行を止めてから実行します。Stop同士も直列です。
    m_Stop.store(true);
    m_Wake.notify_one();

    if (m_Worker.joinable() == true)
    {
        m_Worker.join();
    }
}

RemoteSinkStatistics NetworkDiagnosticSink::GetStatistics() const
{
    std::lock_guard lock(m_Mutex);
    return {m_Enqueued.load(), m_Dropped.load(), m_Sent.load(), m_Connections.load(), m_ConnectionFailures.load(),
        m_Disconnects.load(), m_Queue.size(), m_QueuedBytes, m_LastError.load(), m_NativeError.load()};
}

void NetworkDiagnosticSink::Run() noexcept
{
    try
    {
        Network::WindowsTcpSocket socket; Network::TcpError error;
        RemotePacket packet;
        packet.SessionHigh = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        packet.SessionLow = (static_cast<std::uint64_t>(_getpid()) << 32) | (++NextSession);
        auto stopAt = Clock::time_point::max();
        auto nextStatus = Clock::now();

        auto send = [&](RemotePacketKind kind, std::uint64_t ordinal, const std::string& bytes)
        {
            packet.Kind = kind;
            packet.Ordinal = ordinal;
            packet.NetworkDropped = m_Dropped.load();
            packet.ProducerDropped = GetAsyncDroppedCount();
            packet.Sent = m_Sent.load();
            packet.EventBytes = bytes;

            const bool result = socket.SendAll(EncodeRemotePacket(packet), std::min(Clock::now() + IOTimeout, stopAt), error);

            if (result == false)
            {
                m_LastError.store(static_cast<int>(error.Code));
                m_NativeError.store(error.NativeCode);
            }
            return result;
        };

        for (;;)
        {
            if (m_Stop.load() == true
                && stopAt == Clock::time_point::max()
            ) {
                stopAt = Clock::now() + StopGrace;
            }

            if (Clock::now() >= stopAt)
            {
                break;
            }

            if (socket.IsOpen() == false)
            {
                if (socket.ConnectLoopback(m_Options.Port, std::min(Clock::now() + IOTimeout, stopAt), error) == false)
                {
                    ++m_ConnectionFailures;
                    m_LastError.store(static_cast<int>(error.Code));
                    m_NativeError.store(error.NativeCode);
                    std::unique_lock lock(m_Mutex);
                    m_Wake.wait_for(lock, RetryDelay);
                    continue;
                }
                packet.Connection = m_Connections.fetch_add(1) + 1;

                if (send(RemotePacketKind::Hello, m_Ordinal.load(), {}) == false)
                {
                    ++m_Disconnects;
                    continue;
                }
            }

            QueuedEvent event;
            {
                std::unique_lock lock(m_Mutex);
                if (m_Queue.empty() == false)
                {
                    event = std::move(m_Queue.front());
                    m_Queue.pop_front();
                    m_QueuedBytes -= event.Bytes.size();
                }
                else if (m_Stop.load() == false)
                {
                    m_Wake.wait_for(lock, RetryDelay);
                }
            }

            if (event.Bytes.empty() == false)
            {
                if (send(RemotePacketKind::Event, event.Ordinal, event.Bytes) == true)
                {
                    ++m_Sent;
                }
                else
                {
                    ++m_Dropped;
                    ++m_Disconnects;
                }
            }
            else if (m_Stop.load() == true)
            {
                break;
            }
            else if (Clock::now() >= nextStatus)
            {
                if (send(RemotePacketKind::Status, m_Ordinal.load(), {}) == false)
                {
                    ++m_Disconnects;
                }
                nextStatus = Clock::now() + IOTimeout;
            }
        } // end for

        {
            std::lock_guard lock(m_Mutex);
            m_Dropped.fetch_add(m_Queue.size()); m_Queue.clear(); m_QueuedBytes = 0;
        }

        // Closeはbest effortです。ACKはなく、SentはOS受付であり受信/保存の保証ではありません。
        if (socket.IsOpen() == true
            && send(RemotePacketKind::Close, m_Ordinal.load(), {}) == false
        ) {
            ++m_Disconnects;
        }
    }
    catch (...)
    {
        m_Fatal.store(true);
        std::lock_guard lock(m_Mutex);
        m_Dropped.fetch_add(m_Queue.size());
        m_Queue.clear();
        m_QueuedBytes = 0;

        WriteEmergencyDiagnostic("Remote diagnostics worker failed.");
    }
}

namespace
{

struct ReceiveOptions
{
    std::uint16_t Port = 0;
    std::filesystem::path Output;
    std::uint64_t DurationMs = 300000, MaxBytes = 64u * 1024u * 1024u;
    DiagnosticLanguage Language = DiagnosticLanguage::English;
};

class CaptureFile final
{
public:
    ~CaptureFile()
    {
        if (m_File != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_File);
        }
    }

    bool Open(const std::filesystem::path& path)
    {
        // exists()+ofstreamでは検査とopenの間に競合します。CREATE_NEWで上書き拒否を原子的に行います。
        m_File = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_File != INVALID_HANDLE_VALUE;
    }

    bool Write(std::string_view bytes)
    {
        DWORD written = 0;
        return WriteFile(m_File, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != 0 && written == bytes.size();
    }

    bool Flush()
    {
        return m_File == INVALID_HANDLE_VALUE || FlushFileBuffers(m_File) != 0;
    }

private:
    HANDLE m_File = INVALID_HANDLE_VALUE;
};

int Receive(const ReceiveOptions& options)
{
    Network::WindowsTcpSocket listener;
    Network::TcpError error;

    if (listener.ListenLoopback(options.Port, error) == false)
    {
        std::cerr << "Remote listen failed: " << static_cast<int>(error.Code) << '\n';
        return 1;
    }

    // セッションが確定するまでファイルを作りません。既存Captureの意図しない上書きも拒否します。
    auto metadataPath = options.Output;
    metadataPath += ".remote.json";

    if (std::filesystem::exists(options.Output)
        || std::filesystem::exists(metadataPath)
    ) {
        std::cerr << "Remote output already exists.\n";
        return 1;
    }

    CaptureFile output;
    CaptureFile metadata;

    std::uint64_t sessionHigh = 0;
    std::uint64_t sessionLow = 0;
    std::uint64_t connectionId = 0;
    std::uint64_t lastOrdinal = 0;

    std::uint64_t received = 0;
    std::uint64_t gaps = 0;
    std::uint64_t networkDrops = 0;
    std::uint64_t producerDrops = 0;
    std::uint64_t sent = 0;
    std::uint64_t rejected = 0;
    std::uint64_t interrupted = 0;
    std::uint64_t acceptedConnections = 0;
    std::uint64_t savedBytes = 16;

    bool closed = false;
    bool saveFailed = false;

    const auto deadline = Clock::now() + std::chrono::milliseconds(options.DurationMs);

    std::cout << "REMOTE_READY port=" << listener.GetLocalPort() << std::endl;

    for (std::size_t attempt = 0; attempt < 4096 && Clock::now() < deadline && closed == false && saveFailed == false; ++attempt)
    {
        Network::WindowsTcpSocket socket;
        if (listener.Accept(socket, std::min(deadline, Clock::now() + IOTimeout), error) == false)
        {
            continue;
        }
        bool hello = false;

        RemotePacketParser parser([&](RemotePacket&& packet)
        {
            if (closed == true)
            {
                return false;
            }

            if (hello == false)
            {
                if (packet.Kind != RemotePacketKind::Hello
                    || packet.Connection <= connectionId
                ) {
                    return false;
                }

                if (sessionHigh != 0
                    || sessionLow != 0
                )  {
                    if (sessionHigh != packet.SessionHigh
                        || sessionLow != packet.SessionLow
                    ) {
                        return false;
                    }
                }
                else
                {
                    sessionHigh = packet.SessionHigh;
                    sessionLow = packet.SessionLow;
                    std::ostringstream header;
                    WriteBinaryTraceHeader(header);

                    if (output.Open(options.Output) == false
                        || metadata.Open(metadataPath) == false
                        || output.Write(header.str()) == false
                    ) {
                        saveFailed = true;
                        return false;
                    }
                }

                connectionId = packet.Connection; hello = true; ++acceptedConnections;

            }
            else if (packet.Kind == RemotePacketKind::Hello
                || packet.Connection != connectionId
                || packet.SessionHigh != sessionHigh
                || packet.SessionLow != sessionLow
            ) {
                return false;
            }

            networkDrops = std::max(networkDrops, packet.NetworkDropped);
            producerDrops = std::max(producerDrops, packet.ProducerDropped);
            sent = std::max(sent, packet.Sent);

            if (packet.Kind == RemotePacketKind::Event)
            {
                if (packet.Ordinal == 0
                    || packet.Ordinal <= lastOrdinal
                ) {
                    return false;
                }

                DiagnosticEvent event;
                if (DecodeRemoteEvent(packet, event) == false)
                {
                    return false;
                }
                const auto recordBytes = packet.EventBytes.size() - 16;

                if (recordBytes > options.MaxBytes - savedBytes)
                {
                    saveFailed = true;
                    return false;
                }
                if (output.Write(std::string_view(packet.EventBytes).substr(16)) == false)
                {
                    saveFailed = true;
                    return false;
                }

                gaps += packet.Ordinal - lastOrdinal - 1;
                lastOrdinal = packet.Ordinal; ++received;
                savedBytes += recordBytes;

                if (const auto* message = std::get_if<DiagnosticMessage>(&event.Payload); message != nullptr)
                {
                    std::cout << FormatDiagnosticMessage(event.Id, *message, options.Language).Text << '\n';
                }
                else
                {
                    WriteConsoleEvent(std::cout, event);
                }
            }
            else if (packet.Kind == RemotePacketKind::Close)
            {
                if (packet.Ordinal < lastOrdinal
                    || packet.Sent < received)
                {
                    return false;
                }

                gaps += packet.Ordinal - lastOrdinal; closed = true;
            }
            return true;
        });

        std::array<char, 512> bytes{};
        auto frameDeadline = std::min(deadline, Clock::now() + std::chrono::seconds(1));
        bool valid = true;
        while (Clock::now() < deadline && closed == false && saveFailed == false)
        {
            std::size_t count = 0;
            if (socket.Receive(bytes.data(), bytes.size(), count, frameDeadline, error) == false)
            {
                valid = false;
                break;
            }

            if (count == 0)
            {
                break;
            }

            if (parser.Feed({bytes.data(), count}) == false)
            {
                valid = false;
                ++rejected;
                break;
            }

            if (parser.Finish() == true)
            {
                frameDeadline = std::min(deadline, Clock::now() + std::chrono::seconds(1));
            }
        }

        if (valid == false
            || parser.Finish() == false
            || closed == false
        ) {
            ++interrupted;
        }
    }
    if (output.Flush() == false)
    { 
        saveFailed = true;
    }
    if (sessionHigh != 0
        || sessionLow != 0
    ) {
        // Binary v1.3を変更せず、通信固有のSession/欠落/終了状態は小さなsidecarへ保存します。
        // 最後のStatusを受け取れなかった終了ではclosed=falseを残し、完全なCaptureと扱いません。
        std::ostringstream info;
        info << "{\"protocol\":1,\"sessionHigh\":\"" << sessionHigh << "\",\"sessionLow\":\"" << sessionLow
            << "\",\"received\":" << received << ",\"savedBytes\":" << savedBytes << ",\"ordinalGaps\":" << gaps
            << ",\"networkDrops\":" << networkDrops << ",\"producerDropsProcess\":" << producerDrops << ",\"senderOsSent\":" << sent
            << ",\"connections\":" << acceptedConnections << ",\"rejected\":" << rejected << ",\"interrupted\":" << interrupted
            << ",\"closed\":" << (closed ? "true" : "false") << ",\"saveFailed\":" << (saveFailed ? "true" : "false") << "}\n";

        if (metadata.Write(info.str()) == false
            || metadata.Flush() == false
        ) {
            saveFailed = true;
        }
    }
    std::cout << "REMOTE_SUMMARY session=" << sessionHigh << ':' << sessionLow << " received=" << received << " saved=" << received
        << " bytes=" << savedBytes << " gaps=" << gaps << " network_drops=" << networkDrops << " producer_drops_process=" << producerDrops
        << " sender_os_sent=" << sent << " connections=" << acceptedConnections << " connection_generation=" << connectionId
        << " rejected=" << rejected << " interrupted=" << interrupted
        << " closed=" << closed << " save_failed=" << saveFailed << std::endl;

    return closed == true && saveFailed == false ? 0 : 1;
}

bool Number(std::string_view text, std::uint64_t& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return text.empty() == false && result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

int SendSample(std::uint16_t port)
{
    auto sink = std::make_unique<NetworkDiagnosticSink>(RemoteSinkOptions{port});
    auto* network = sink.get();

    std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
    sinks.push_back(std::move(sink));

    DiagnosticSession session(std::move(sinks));

    if (session.IsActive() == false)
    {
        return 1;
    }
    SetMeasurementsEnabled(true);
    BeginDiagnosticFrame();

    Emit(0x9001, DiagnosticCategory::Core, DiagnosticSeverity::Info, std::string("remote sample"));

    EmitStructuredMessage(Messages::PhysicsGravityUpdated, Messages::MakePhysicsGravityUpdated(0, -9.5, 0));

    RecordCounter("Remote.Sample", 7);

    {
        TraceScope trace("Remote.Scope");
    }

    FlushDiagnostics();

    const auto sampleDeadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < sampleDeadline)
    {
        const auto progress = network->GetStatistics();
        if (progress.Sent + progress.Dropped >= 5)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    network->Stop();
    const auto stats = network->GetStatistics();

    std::cout << "REMOTE_SENT enqueued=" << stats.Enqueued << " sent=" << stats.Sent << " dropped=" << stats.Dropped << '\n';
    return stats.Sent == 5 && stats.Dropped == 0 ? 0 : 1;
}

} // end namespace

bool TryRunRemoteDiagnostics(int argc, char** argv, int& result)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument.starts_with("--diagnostics-inspect="))
        {
            result = 1;
            if (argc != 2)
            {
                return true;
            }
            std::ifstream file(std::string(argument.substr(22)), std::ios::binary);

            BinaryTraceReader reader(file);
            DiagnosticEvent event;
            TraceReadResult read;

            std::uint64_t count = 0;
            std::uint64_t counters = 0;
            std::uint64_t begins = 0;
            std::uint64_t ends = 0;
            std::uint64_t messages = 0;

            while ((read = reader.ReadNext(event)) == TraceReadResult::Event)
            {
                ++count;
                if (const auto* metric = std::get_if<DiagnosticMetric>(&event.Payload); metric != nullptr)
                {
                    counters += metric->Kind == DiagnosticMetricKind::Counter;
                    begins += metric->Kind == DiagnosticMetricKind::TraceBegin;
                    ends += metric->Kind == DiagnosticMetricKind::TraceEnd;
                }
                messages += std::holds_alternative<DiagnosticMessage>(event.Payload);
            }
            std::cout << "REMOTE_READ events=" << count << " counters=" << counters << " begins=" << begins << " ends=" << ends << " messages=" << messages << '\n';
            result = read == TraceReadResult::EndOfStream ? 0 : 1; return true;
        }
    }
    bool receive = false;
    bool sample = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view value(argv[index]);
        receive = receive || value.starts_with("--diagnostics-receive=");
        sample = sample || value.starts_with("--diagnostics-send-test=");
    }
    if (receive == false && sample == false)
    {
        return false;
    }

    result = 1;
    ReceiveOptions options;

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg(argv[index]);
        const auto equal = arg.find('=');
        if (equal == std::string_view::npos)
        {
            std::cerr << "Invalid remote option.\n";
            return true;
        }

        const auto name = arg.substr(0, equal);
        const auto value = arg.substr(equal + 1);

        std::uint64_t number = 0;

        if (name == "--diagnostics-output")
        {
            options.Output = std::string(value);
        }
        else if (name == "--diagnostics-language" && (value == "en" || value == "ja"))
        {
            options.Language = value == "ja" ? DiagnosticLanguage::Japanese : DiagnosticLanguage::English;
        }
        else if (Number(value, number) == false)
        {
            std::cerr << "Invalid remote numeric option.\n";
            return true;
        }
        else if ((name == "--diagnostics-receive" || name == "--diagnostics-send-test") && number > 0 && number <= 65535)
        {
            options.Port = static_cast<std::uint16_t>(number);
        }
        else if (name == "--diagnostics-duration-ms" && number >= 100 && number <= 3600000)
        {
            options.DurationMs = number;
        }
        else if (name == "--diagnostics-max-bytes" && number >= 16 && number <= 1024ull * 1024ull * 1024ull)
        {
            options.MaxBytes = number;
        }
        else
        {
            std::cerr << "Invalid remote option.\n";
            return true;
        }
    }
    if (receive == sample
        || options.Port == 0
        || (receive == true && options.Output.empty())
    ) {
        std::cerr << "Conflicting/missing remote options.\n";
        return true;
    }
    try
    {
        result = (receive == true);

        if (receive)
        {
            Receive(options);
        }
        else
        {
            SendSample(options.Port);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Remote diagnostics: " << error.what() << '\n';
    }
    return true;
}

} // end Raven::Diagnostics

#endif
