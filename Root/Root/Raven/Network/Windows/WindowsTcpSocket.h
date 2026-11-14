#pragma once

#ifdef _WIN32
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Raven
{
namespace Network
{
class ControlMessageReceiver;
using TcpDeadline = std::chrono::steady_clock::time_point;
enum class TcpErrorCode
{
    None, InvalidState, InvalidArgument, Startup, Socket, Bind, Listen,
    Connect, Accept, Wait, Send, Receive, Shutdown, Timeout, Protocol
};
struct TcpError
{
    TcpErrorCode Code = TcpErrorCode::None;
    int NativeCode = 0;
};

// Windows/IPv4 Loopbackに限定した試験Transport。SocketとWSAStartup参照を一緒に所有する。
// 内部Threadは作らない。各Instanceは単一Threadで使用し、移譲前に旧Threadをjoinすること。
// Close/破棄も実行中I/Oと並行させない。待機は絶対期限で制限する。
class WindowsTcpSocket
{
public:
    WindowsTcpSocket();
    ~WindowsTcpSocket();
    WindowsTcpSocket(const WindowsTcpSocket&) = delete;
    WindowsTcpSocket& operator=(const WindowsTcpSocket&) = delete;
    WindowsTcpSocket(WindowsTcpSocket&& other) noexcept;
    WindowsTcpSocket& operator=(WindowsTcpSocket&& other) noexcept;

    // port=0はOSに空きPortを選ばせる。外部InterfaceにはBindしない。
    bool ListenLoopback(std::uint16_t port, TcpError& error);
    bool ConnectLoopback(std::uint16_t port, TcpDeadline deadline, TcpError& error);
    // 失敗時はoutputを保持。outputはClosedでなければならない。
    bool Accept(WindowsTcpSocket& output, TcpDeadline deadline, TcpError& error);
    // 部分Sendを期限内で継続。I/O失敗/Timeout時は部分Frameの再送を防ぐため接続を閉じる。
    // 状態不正による呼び出し拒否では既存Socketを変更しない。
    // 成功はOSによる受付であり、Peer側の検証・処理完了を保証しない。
    bool SendAll(std::string_view bytes, TcpDeadline deadline, TcpError& error);
    // count=0はEOF（Socketも閉じる）。Timeoutだけは再試行可、他のI/O失敗は閉じる。
    // 失敗時count=0。成功分だけbufferに書き込み、Viewは保持しない。
    bool Receive(void* buffer, std::size_t capacity, std::size_t& count,
        TcpDeadline deadline, TcpError& error);
    bool ShutdownSend(TcpError& error);
    void Close();
    bool IsOpen() const;
    std::uint16_t GetLocalPort() const;
private:
    enum class Role { Closed, Listener, Connection };
    bool Startup(TcpError& error);
    bool Configure(TcpError& error);
    bool UpdateLocalPort(TcpError& error);
    bool Wait(bool writing, TcpDeadline deadline, TcpError& error);
    void Swap(WindowsTcpSocket& other) noexcept;

    std::uintptr_t m_Handle;
    bool m_Started = false;
    Role m_Role = Role::Closed;
    std::uint16_t m_LocalPort = 0;
    bool m_SendShutdown = false;
};

// 1回のReceiveを既存Framerへ接続。成功でも部分Frameの場合がある。
// EOFでFinishStream、Timeout/Socket/Protocol失敗でClose+未処理値破棄。
// 複数回呼ぶ場合も同じ絶対期限を渡し、遅い部分受信で期限を延長しないこと。
bool ReceiveControlStream(WindowsTcpSocket& socket, ControlMessageReceiver& receiver,
    TcpDeadline deadline, TcpError& error);
}
}
#endif
