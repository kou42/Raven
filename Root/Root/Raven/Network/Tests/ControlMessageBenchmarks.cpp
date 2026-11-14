// 専用exeだけで有効にし、通常アプリ・回帰テストへmainや計測負荷を追加しない。
#if defined(_WIN32) && defined(RAVEN_NETWORK_BENCHMARK_MAIN)
#include "Raven/Network/ControlMessageReceiver.h"
#include "Raven/Network/Windows/WindowsTcpSocket.h"
#include "Raven/Data/Tests/DataBenchmarkMemory.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Raven
{
namespace Network
{
namespace Tests
{
namespace
{
constexpr std::size_t Warmups = 5;
constexpr std::size_t TimeSamples = 101;
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
constexpr std::size_t Samples = 1;
#else
constexpr std::size_t Samples = TimeSamples;
#endif
using Clock = std::chrono::steady_clock;
void Require(bool value)
{
    if (value == false)
    {
        throw std::runtime_error("network benchmark validation failed");
    }
}
bool Same(const ControlMessage& left, const ControlMessage& right)
{
    if (left.RequestId != right.RequestId || left.Payload.index() != right.Payload.index())
    {
        return false;
    }
    const auto* ping = std::get_if<PingMessage>(&left.Payload);
    if (ping != nullptr)
    {
        return ping->Nonce == std::get<PingMessage>(right.Payload).Nonce;
    }
    return std::get<SetReadyMessage>(left.Payload).Ready == std::get<SetReadyMessage>(right.Payload).Ready;
}
struct Workload
{
    std::string Name;
    std::vector<ControlMessage> Messages;
    std::vector<std::string> Encoded;
    std::string Frames;
    std::size_t PayloadBytes = 0;
};
Workload MakeWorkload(bool mixed, std::size_t batch)
{
    Workload result;
    result.Name = mixed ? "mixed" : "ping";
    MessageError error;
    for (std::size_t i = 0; i < batch; ++i)
    {
        ControlMessage message{100000u + i, PingMessage{(std::numeric_limits<std::uint64_t>::max)() - i}};
        if (mixed && i % 2 == 1)
        {
            message.Payload = SetReadyMessage{i % 4 == 1};
        }
        std::string bytes;
        std::string frame;
        Require(ControlMessageCodec::Encode(message, bytes, error));
        Require(ControlMessageCodec::EncodeStreamFrame(message, frame, error));
        result.PayloadBytes += bytes.size();
        result.Messages.push_back(message);
        result.Encoded.push_back(std::move(bytes));
        result.Frames.append(frame);
    }
    return result;
}

class CheckingHandler final : public IControlMessageHandler
{
public:
    explicit CheckingHandler(const Workload& workload) : m_Workload(workload) {}
    void OnPing(std::uint64_t request, const PingMessage& value) override
    {
        Check({request, value});
    }
    void OnSetReady(std::uint64_t request, const SetReadyMessage& value) override
    {
        Check({request, value});
    }
    bool Good() const
    {
        return m_Good && m_Count == m_Workload.Messages.size();
    }
private:
    void Check(const ControlMessage& value)
    {
        m_Good = m_Good && m_Count < m_Workload.Messages.size()
            && Same(value, m_Workload.Messages[m_Count]);
        ++m_Count;
    }
    // FixtureはSampleとHandlerより長く生存し、計測中は変更しない。
    const Workload& m_Workload;
    std::size_t m_Count = 0;
    bool m_Good = true;
};

enum class Stage { Encode, Decode, ReceiveQueue, QueueDispatch };
const char* StageName(Stage stage)
{
    switch (stage)
    {
    case Stage::Encode: return "encode";
    case Stage::Decode: return "decode";
    case Stage::ReceiveQueue: return "receive_queue";
    case Stage::QueueDispatch: return "queue_dispatch";
    }
    return "invalid";
}
class CpuSample
{
public:
    CpuSample(const Workload& workload, Stage stage)
        : m_Workload(workload), m_Stage(stage), m_Handler(workload),
          m_Encoded(workload.Messages.size()), m_Decoded(workload.Messages.size())
    {
        // Queue単独のPop/Dispatchは、検証・投入を計測区間の外で済ませる。
        if (stage == Stage::QueueDispatch)
        {
            Require(m_Receiver.ReceiveStream(workload.Frames));
        }
    }
    bool Run()
    {
        MessageError error;
        switch (m_Stage)
        {
        case Stage::Encode:
            for (std::size_t i = 0; i < m_Workload.Messages.size(); ++i)
            {
                if (ControlMessageCodec::Encode(m_Workload.Messages[i], m_Encoded[i], error) == false)
                {
                    return false;
                }
            }
            return true;
        case Stage::Decode:
            for (std::size_t i = 0; i < m_Workload.Messages.size(); ++i)
            {
                if (ControlMessageCodec::Decode(m_Workload.Encoded[i], m_Decoded[i], error) == false)
                {
                    return false;
                }
            }
            return true;
        case Stage::ReceiveQueue:
            return m_Receiver.ReceiveStream(m_Workload.Frames);
        case Stage::QueueDispatch:
            return Drain();
        }
        return false;
    }
    bool Verify()
    {
        switch (m_Stage)
        {
        case Stage::Encode: return m_Encoded == m_Workload.Encoded;
        case Stage::Decode:
            for (std::size_t i = 0; i < m_Decoded.size(); ++i)
            {
                if (Same(m_Decoded[i], m_Workload.Messages[i]) == false)
                {
                    return false;
                }
            }
            return true;
        case Stage::ReceiveQueue: return Drain();
        case Stage::QueueDispatch: return m_Handler.Good() && m_Receiver.GetQueuedBytes() == 0;
        }
        return false;
    }
private:
    bool Drain()
    {
        MessageError error;
        ControlMessage message;
        while (m_Receiver.TryPop(message))
        {
            if (ControlMessageCodec::Dispatch(message, m_Handler, error) == false)
            {
                return false;
            }
        }
        return m_Handler.Good() && m_Receiver.GetQueuedBytes() == 0;
    }
    const Workload& m_Workload;
    Stage m_Stage;
    CheckingHandler m_Handler;
    ControlMessageReceiver m_Receiver{MessageTransport::Stream};
    std::vector<std::string> m_Encoded;
    std::vector<ControlMessage> m_Decoded;
};

struct Measurements
{
    std::vector<double> Microseconds;
    Data::Tests::BenchmarkMemory Memory;
};
class MeasurementScope
{
public:
    explicit MeasurementScope(Measurements& output) : m_Output(output)
    {
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
        Data::Tests::BeginBenchmarkMemory();
#else
        m_Start = Clock::now();
#endif
    }
    ~MeasurementScope()
    {
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
        m_Output.Memory = Data::Tests::EndBenchmarkMemory();
#else
        const auto end = Clock::now();
        // vectorは事前Reserve済み。計測結果の保存は測定終了後に行う。
        m_Output.Microseconds.push_back(std::chrono::duration<double, std::micro>(end - m_Start).count());
#endif
    }
private:
    Measurements& m_Output;
    Clock::time_point m_Start;
};

void Print(const Workload& workload, const char* stage, Measurements& result, bool tcp)
{
    std::cout << workload.Name << ',' << stage << ',' << workload.Messages.size() << ',' << Samples
        << ',' << workload.PayloadBytes << ',' << workload.Frames.size()
        << ',' << (tcp ? 2 * workload.Frames.size() : 0) << ',' << Samples * workload.Messages.size();
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
    std::cout << ",NA,NA," << result.Memory.Allocations << ',' << result.Memory.Bytes
        << ',' << result.Memory.PeakBytes << ",NA\n";
#else
    auto sorted = result.Microseconds;
    std::sort(sorted.begin(), sorted.end());
    // nearest-rank: ceil(0.95*N)-1。中央値も実際のSampleを使用する。
    const auto p95 = (95 * sorted.size() + 99) / 100 - 1;
    std::cout << ',' << sorted[sorted.size() / 2] << ',' << sorted[p95] << ",NA,NA,NA,";
    for (std::size_t i = 0; i < result.Microseconds.size(); ++i)
    {
        if (i != 0)
        {
            std::cout << ';';
        }
        std::cout << result.Microseconds[i];
    }
    std::cout << '\n';
#endif
}
void MeasureCpu(const Workload& workload, Stage stage)
{
    for (std::size_t i = 0; i < Warmups; ++i)
    {
        CpuSample sample(workload, stage);
        Require(sample.Run() && sample.Verify());
    }
    Measurements result;
    result.Microseconds.reserve(Samples);
    for (std::size_t i = 0; i < Samples; ++i)
    {
        CpuSample sample(workload, stage);
        bool success = false;
        {
            MeasurementScope scope(result);
            success = sample.Run();
        }
        Require(success && sample.Verify());
    }
    Print(workload, StageName(stage), result, false);
}

// WorkerはSocket/Receiverを所有し、Main Threadは別Socketだけを使う。
// Scope途中の失敗でもClient CloseでPeerを起こし、join後にFixture等を破棄する。
struct JoinWorker
{
    std::thread& Thread;
    WindowsTcpSocket& Client;
    ~JoinWorker()
    {
        Client.Close();
        if (Thread.joinable())
        {
            Thread.join();
        }
    }
};
void MeasureTcp(const Workload& workload)
{
    TcpError error;
    WindowsTcpSocket listener;
    WindowsTcpSocket client;
    WindowsTcpSocket server;
    const auto connectDeadline = Clock::now() + std::chrono::seconds(5);
    const bool connected = listener.ListenLoopback(0, error)
        && client.ConnectLoopback(listener.GetLocalPort(), connectDeadline, error)
        && listener.Accept(server, connectDeadline, error);
    if (connected == false)
    {
        std::cerr << "TCP setup: code=" << static_cast<int>(error.Code) << ", native=" << error.NativeCode << '\n';
    }
    Require(connected);
    listener.Close();
    std::atomic<bool> ready{false};
    std::atomic<bool> workerGood{true};
    std::atomic<std::size_t> completed{0};
    std::thread worker([&, owned = std::move(server)]() mutable
    {
        try
        {
            ControlMessageReceiver incoming(MessageTransport::Stream);
            std::string reply;
            reply.reserve(workload.Frames.size());
            ready = true;
            for (std::size_t iteration = 0; iteration < Warmups + Samples; ++iteration)
            {
                const auto deadline = Clock::now() + std::chrono::seconds(5);
                std::size_t received = 0;
                reply.clear();
                while (received < workload.Messages.size())
                {
                    TcpError transportError;
                    Require(ReceiveControlStream(owned, incoming, deadline, transportError) && owned.IsOpen());
                    ControlMessage message;
                    while (incoming.TryPop(message))
                    {
                        Require(received < workload.Messages.size() && Same(message, workload.Messages[received]));
                        MessageError codecError;
                        std::string frame;
                        Require(ControlMessageCodec::EncodeStreamFrame(message, frame, codecError));
                        reply.append(frame);
                        ++received;
                    }
                }
                TcpError transportError;
                Require(owned.SendAll(reply, deadline, transportError));
                // Sample内のWorker確保/解放が終わった後にMain側で測定を終了する。
                completed = iteration + 1;
            }
        }
        catch (...)
        {
            workerGood = false;
            ready = true;
        }
    });
    JoinWorker join{worker, client};
    const auto readyDeadline = Clock::now() + std::chrono::seconds(5);
    while (ready.load() == false && Clock::now() < readyDeadline)
    {
        std::this_thread::yield();
    }
    Require(ready && workerGood);
    ControlMessageReceiver incoming(MessageTransport::Stream);
    Measurements result;
    result.Microseconds.reserve(Samples);
    for (std::size_t iteration = 0; iteration < Warmups + Samples; ++iteration)
    {
        auto operation = [&]()
        {
            const auto deadline = Clock::now() + std::chrono::seconds(5);
            CheckingHandler handler(workload);
            TcpError transportError;
            if (client.SendAll(workload.Frames, deadline, transportError) == false)
            {
                return false;
            }
            std::size_t received = 0;
            while (received < workload.Messages.size())
            {
                if (ReceiveControlStream(client, incoming, deadline, transportError) == false)
                {
                    return false;
                }
                ControlMessage message;
                MessageError codecError;
                while (incoming.TryPop(message))
                {
                    if (ControlMessageCodec::Dispatch(message, handler, codecError) == false)
                    {
                        return false;
                    }
                    ++received;
                }
            }
            while (completed.load() < iteration + 1 && workerGood && Clock::now() < deadline)
            {
                std::this_thread::yield();
            }
            return handler.Good() && incoming.GetQueuedCount() == 0 && incoming.GetQueuedBytes() == 0
                && completed.load() == iteration + 1 && workerGood;
        };
        bool success = false;
        if (iteration < Warmups)
        {
            success = operation();
        }
        else
        {
            MeasurementScope scope(result);
            success = operation();
        }
        Require(success);
    }
    worker.join();
    Require(workerGood);
    Print(workload, "tcp_round_trip", result, true);
}
}

int RunControlMessageBenchmarks()
{
    try
    {
        std::cerr << "compiler_msc_full_ver=" << _MSC_FULL_VER << "; warmups=" << Warmups
            << "; time_samples=" << TimeSamples << "; cplusplus=" << _MSVC_LANG << '\n';
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "workload,stage,batch,samples,payload_bytes,frame_bytes,tcp_round_trip_bytes,checked_messages,median_us,p95_us,allocations,requested_bytes,peak_bytes,samples_us\n";
        for (const bool mixed : {false, true})
        {
            for (const std::size_t batch : {1u, 16u, 64u})
            {
                const auto workload = MakeWorkload(mixed, batch);
                for (const auto stage : {Stage::Encode, Stage::Decode, Stage::ReceiveQueue, Stage::QueueDispatch})
                {
                    MeasureCpu(workload, stage);
                }
                MeasureTcp(workload);
            }
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
}
}
}
int main()
{
    return Raven::Network::Tests::RunControlMessageBenchmarks();
}
#endif
