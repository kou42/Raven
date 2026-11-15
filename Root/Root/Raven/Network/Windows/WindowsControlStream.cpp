#ifdef _WIN32
#include "Raven/Network/Windows/WindowsTcpSocket.h"
#include "Raven/Network/ControlMessageReceiver.h"
#include <array>

namespace Raven::Network
{
// 制御Message固有のAdapterはRaw Socketから分離し、診断通信へCodec依存を持ち込みません。
bool ReceiveControlStream(WindowsTcpSocket& socket, ControlMessageReceiver& receiver,
    TcpDeadline deadline, TcpError& error)
{
    if (receiver.GetState() != ReceiverState::Open)
    {
        socket.Close();
        error = {TcpErrorCode::Protocol};
        return false;
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
        error = {TcpErrorCode::Protocol};
        return false;
    }
    return true;
}
}
#endif
