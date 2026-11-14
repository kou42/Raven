#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include "Raven/Network/Windows/WindowsTcpSocket.h"
#include "Raven/Network/ControlMessageReceiver.h"
#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace Raven
{
namespace Network
{
namespace
{
constexpr std::uintptr_t InvalidSocket = static_cast<std::uintptr_t>(INVALID_SOCKET);
// 大きな入力も1回のSendを64 KiBに抑え、部分送信ごとに期限を再検査する。
constexpr std::size_t SendChunkBytes = 64u * 1024u;
bool Fail(TcpError& error, TcpErrorCode code, int native = 0)
{
    error = {code, native};
    return false;
}
bool Expired(TcpDeadline deadline, TcpError& error)
{
    if (std::chrono::steady_clock::now() >= deadline)
    {
        error = {TcpErrorCode::Timeout};
        return true;
    }
    return false;
}
sockaddr_in LoopbackAddress(std::uint16_t port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}
}

WindowsTcpSocket::WindowsTcpSocket() : m_Handle(InvalidSocket)
{
}
WindowsTcpSocket::~WindowsTcpSocket()
{
    Close();
}
WindowsTcpSocket::WindowsTcpSocket(WindowsTcpSocket&& other) noexcept : WindowsTcpSocket()
{
    Swap(other);
}
WindowsTcpSocket& WindowsTcpSocket::operator=(WindowsTcpSocket&& other) noexcept
{
    if (this != &other)
    {
        Close();
        Swap(other);
    }
    return *this;
}
void WindowsTcpSocket::Swap(WindowsTcpSocket& other) noexcept
{
    using std::swap;
    swap(m_Handle, other.m_Handle);
    swap(m_Started, other.m_Started);
    swap(m_Role, other.m_Role);
    swap(m_LocalPort, other.m_LocalPort);
    swap(m_SendShutdown, other.m_SendShutdown);
}
void WindowsTcpSocket::Close()
{
    // Socketを先に閉じ、最後に自分のWSAStartup参照を解放する。
    // Accepted Socketも独立した参照を持ち、Listener破棄後も利用できる。
    if (m_Handle != InvalidSocket)
    {
        closesocket(static_cast<SOCKET>(m_Handle));
        m_Handle = InvalidSocket;
    }
    if (m_Started == true)
    {
        WSACleanup();
        m_Started = false;
    }
    m_Role = Role::Closed;
    m_LocalPort = 0;
    m_SendShutdown = false;
}
bool WindowsTcpSocket::IsOpen() const
{
    return m_Handle != InvalidSocket;
}
std::uint16_t WindowsTcpSocket::GetLocalPort() const
{
    return m_LocalPort;
}
bool WindowsTcpSocket::Startup(TcpError& error)
{
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
    {
        return Fail(error, TcpErrorCode::Startup, result);
    }
    m_Started = true;
    if (data.wVersion != MAKEWORD(2, 2))
    {
        return Fail(error, TcpErrorCode::Startup, WSAVERNOTSUPPORTED);
    }
    return true;
}
bool WindowsTcpSocket::Configure(TcpError& error)
{
    u_long nonblocking = 1;
    if (ioctlsocket(static_cast<SOCKET>(m_Handle), FIONBIO, &nonblocking) != 0)
    {
        return Fail(error, TcpErrorCode::Socket, WSAGetLastError());
    }
    // 非Blocking SocketのCloseを待機にしない。配送完了は上位Protocolの応答で確認する。
    linger immediate{0, 0};
    if (setsockopt(static_cast<SOCKET>(m_Handle), SOL_SOCKET, SO_LINGER,
        reinterpret_cast<const char*>(&immediate), sizeof(immediate)) != 0)
    {
        return Fail(error, TcpErrorCode::Socket, WSAGetLastError());
    }
    return true;
}
bool WindowsTcpSocket::UpdateLocalPort(TcpError& error)
{
    sockaddr_in address{};
    int size = sizeof(address);
    if (getsockname(static_cast<SOCKET>(m_Handle), reinterpret_cast<sockaddr*>(&address), &size) != 0)
    {
        return Fail(error, TcpErrorCode::Socket, WSAGetLastError());
    }
    m_LocalPort = ntohs(address.sin_port);
    return true;
}
bool WindowsTcpSocket::Wait(bool writing, TcpDeadline deadline, TcpError& error)
{
    if (Expired(deadline, error))
    {
        return false;
    }
    const SOCKET handle = static_cast<SOCKET>(m_Handle);
    fd_set ready;
    fd_set exceptions;
    FD_ZERO(&ready);
    FD_ZERO(&exceptions);
    FD_SET(handle, &ready);
    FD_SET(handle, &exceptions);
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    // Windows timevalのlong範囲に丸める。戻った後のI/O側でも同じ絶対期限を再検査する。
    const auto seconds = (std::min)(remaining > 0 ? remaining / 1000000 : 0,
        static_cast<decltype(remaining)>((std::numeric_limits<long>::max)()));
    timeval timeout{static_cast<long>(seconds), static_cast<long>(remaining > 0 ? remaining % 1000000 : 0)};
    const int result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, &exceptions, &timeout);
    if (result == SOCKET_ERROR)
    {
        return Fail(error, TcpErrorCode::Wait, WSAGetLastError());
    }
    if (result == 0)
    {
        return Fail(error, TcpErrorCode::Timeout);
    }
    if (FD_ISSET(handle, &exceptions))
    {
        // 非Blocking Connectの失敗はexceptfdsに出る。成功扱いせずSO_ERRORを取得する。
        int native = 0;
        int size = sizeof(native);
        if (getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&native), &size) != 0)
        {
            native = WSAGetLastError();
        }
        return Fail(error, TcpErrorCode::Wait, native);
    }
    return true;
}

bool WindowsTcpSocket::ListenLoopback(std::uint16_t port, TcpError& error)
{
    error = {};
    if (IsOpen())
    {
        return Fail(error, TcpErrorCode::InvalidState);
    }
    WindowsTcpSocket temporary;
    if (temporary.Startup(error) == false)
    {
        return false;
    }
    temporary.m_Handle = static_cast<std::uintptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (temporary.IsOpen() == false)
    {
        return Fail(error, TcpErrorCode::Socket, WSAGetLastError());
    }
    BOOL exclusive = TRUE;
    if (setsockopt(static_cast<SOCKET>(temporary.m_Handle), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0 || temporary.Configure(error) == false)
    {
        if (error.Code == TcpErrorCode::None)
        {
            Fail(error, TcpErrorCode::Socket, WSAGetLastError());
        }
        return false;
    }
    const auto address = LoopbackAddress(port);
    if (bind(static_cast<SOCKET>(temporary.m_Handle), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        return Fail(error, TcpErrorCode::Bind, WSAGetLastError());
    }
    if (listen(static_cast<SOCKET>(temporary.m_Handle), SOMAXCONN) != 0)
    {
        return Fail(error, TcpErrorCode::Listen, WSAGetLastError());
    }
    if (temporary.UpdateLocalPort(error) == false)
    {
        return false;
    }
    temporary.m_Role = Role::Listener;
    Swap(temporary);
    return true;
}

bool WindowsTcpSocket::ConnectLoopback(std::uint16_t port, TcpDeadline deadline, TcpError& error)
{
    error = {};
    if (IsOpen())
    {
        return Fail(error, TcpErrorCode::InvalidState);
    }
    if (port == 0)
    {
        return Fail(error, TcpErrorCode::InvalidArgument);
    }
    if (Expired(deadline, error))
    {
        return false;
    }
    WindowsTcpSocket temporary;
    if (temporary.Startup(error) == false)
    {
        return false;
    }
    temporary.m_Handle = static_cast<std::uintptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (temporary.IsOpen() == false)
    {
        return Fail(error, TcpErrorCode::Socket, WSAGetLastError());
    }
    if (temporary.Configure(error) == false)
    {
        return false;
    }
    const auto address = LoopbackAddress(port);
    const int result = connect(static_cast<SOCKET>(temporary.m_Handle),
        reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result != 0)
    {
        const int native = WSAGetLastError();
        if (native != WSAEWOULDBLOCK)
        {
            return Fail(error, TcpErrorCode::Connect, native);
        }
        if (temporary.Wait(true, deadline, error) == false)
        {
            if (error.Code == TcpErrorCode::Wait)
            {
                error.Code = TcpErrorCode::Connect;
            }
            return false;
        }
        int completion = 0;
        int size = sizeof(completion);
        if (getsockopt(static_cast<SOCKET>(temporary.m_Handle), SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&completion), &size) != 0)
        {
            return Fail(error, TcpErrorCode::Connect, WSAGetLastError());
        }
        if (completion != 0)
        {
            return Fail(error, TcpErrorCode::Connect, completion);
        }
    }
    if (Expired(deadline, error) || temporary.UpdateLocalPort(error) == false)
    {
        return false;
    }
    temporary.m_Role = Role::Connection;
    Swap(temporary);
    return true;
}

bool WindowsTcpSocket::Accept(WindowsTcpSocket& output, TcpDeadline deadline, TcpError& error)
{
    error = {};
    if (m_Role != Role::Listener || output.IsOpen())
    {
        return Fail(error, TcpErrorCode::InvalidState);
    }
    WindowsTcpSocket temporary;
    if (temporary.Startup(error) == false)
    {
        return false;
    }
    while (Expired(deadline, error) == false)
    {
        const SOCKET accepted = accept(static_cast<SOCKET>(m_Handle), nullptr, nullptr);
        if (accepted != INVALID_SOCKET)
        {
            temporary.m_Handle = static_cast<std::uintptr_t>(accepted);
            if (temporary.Configure(error) == false || temporary.UpdateLocalPort(error) == false)
            {
                return false;
            }
            temporary.m_Role = Role::Connection;
            output.Swap(temporary);
            return true;
        }
        const int native = WSAGetLastError();
        if (native != WSAEWOULDBLOCK)
        {
            return Fail(error, TcpErrorCode::Accept, native);
        }
        if (Wait(false, deadline, error) == false)
        {
            return false;
        }
    }
    return false;
}

bool WindowsTcpSocket::SendAll(std::string_view bytes, TcpDeadline deadline, TcpError& error)
{
    error = {};
    if (m_Role != Role::Connection || m_SendShutdown)
    {
        return Fail(error, TcpErrorCode::InvalidState);
    }
    while (bytes.empty() == false)
    {
        if (Expired(deadline, error))
        {
            Close();
            return false;
        }
        const int length = static_cast<int>((std::min)(bytes.size(), SendChunkBytes));
        const int result = send(static_cast<SOCKET>(m_Handle), bytes.data(), length, 0);
        if (result > 0)
        {
            bytes.remove_prefix(static_cast<std::size_t>(result));
            continue;
        }
        const int native = result == 0 ? WSAECONNRESET : WSAGetLastError();
        if (native != WSAEWOULDBLOCK)
        {
            Close();
            return Fail(error, TcpErrorCode::Send, native);
        }
        if (Wait(true, deadline, error) == false)
        {
            Close();
            return false;
        }
    }
    return true;
}

bool WindowsTcpSocket::Receive(void* buffer, std::size_t capacity, std::size_t& count,
    TcpDeadline deadline, TcpError& error)
{
    error = {};
    count = 0;
    if (m_Role != Role::Connection)
    {
        return Fail(error, TcpErrorCode::InvalidState);
    }
    if (buffer == nullptr || capacity == 0)
    {
        return Fail(error, TcpErrorCode::InvalidArgument);
    }
    while (Expired(deadline, error) == false)
    {
        const int length = static_cast<int>((std::min)(capacity,
            static_cast<std::size_t>((std::numeric_limits<int>::max)())));
        const int result = recv(static_cast<SOCKET>(m_Handle), static_cast<char*>(buffer), length, 0);
        if (result > 0)
        {
            count = static_cast<std::size_t>(result);
            return true;
        }
        if (result == 0)
        {
            Close();
            return true;
        }
        const int native = WSAGetLastError();
        if (native != WSAEWOULDBLOCK)
        {
            Close();
            return Fail(error, TcpErrorCode::Receive, native);
        }
        if (Wait(false, deadline, error) == false)
        {
            if (error.Code != TcpErrorCode::Timeout)
            {
                Close();
            }
            return false;
        }
    }
    return false;
}

bool WindowsTcpSocket::ShutdownSend(TcpError& error)
{
    error = {};
    if (m_Role != Role::Connection)
    {
        return Fail(error, TcpErrorCode::InvalidState);
    }
    if (m_SendShutdown == true)
    {
        return true;
    }
    if (shutdown(static_cast<SOCKET>(m_Handle), SD_SEND) != 0)
    {
        const int native = WSAGetLastError();
        Close();
        return Fail(error, TcpErrorCode::Shutdown, native);
    }
    m_SendShutdown = true;
    return true;
}

bool ReceiveControlStream(WindowsTcpSocket& socket, ControlMessageReceiver& receiver,
    TcpDeadline deadline, TcpError& error)
{
    if (receiver.GetState() != ReceiverState::Open)
    {
        socket.Close();
        return Fail(error, TcpErrorCode::Protocol);
    }
    std::array<char, MaxControlMessageBytes> bytes{};
    std::size_t count = 0;
    if (socket.Receive(bytes.data(), bytes.size(), count, deadline, error) == false)
    {
        socket.Close();
        receiver.Disconnect();
        return false;
    }
    const bool accepted = count == 0 ? receiver.FinishStream() : receiver.ReceiveStream({bytes.data(), count});
    if (accepted == false)
    {
        socket.Close();
        receiver.Disconnect();
        return Fail(error, TcpErrorCode::Protocol);
    }
    return true;
}
}
}
#endif
