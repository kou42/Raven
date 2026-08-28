#include "Raven/Debug/BrowserDebugServer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

namespace Raven
{
namespace
{
#ifdef _WIN32
constexpr uintptr_t InvalidSocketValue = static_cast<uintptr_t>(INVALID_SOCKET);
#endif

std::string GetContentType(const std::filesystem::path& filePath)
{
    const std::string extension = filePath.extension().string();
    if (extension == ".html")
    {
        return "text/html; charset=utf-8";
    }
    if (extension == ".svg")
    {
        return "image/svg+xml; charset=utf-8";
    }
    return "application/octet-stream";
}

bool ParseUnsigned(std::string_view value, uint32_t& outValue)
{
    if (value.empty() == true)
    {
        return false;
    }

    uint32_t parsedValue = 0u;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsedValue);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return false;
    }

    outValue = parsedValue;
    return true;
}

BrowserDebugFilterState ParseFilterQuery(std::string_view query)
{
    BrowserDebugFilterState state{};

    std::size_t position = 0u;
    while (position <= query.size())
    {
        const std::size_t ampersand = query.find('&', position);
        const std::size_t tokenEnd = ampersand == std::string_view::npos ? query.size() : ampersand;
        const std::string_view token = query.substr(position, tokenEnd - position);
        const std::size_t equals = token.find('=');

        if (equals != std::string_view::npos)
        {
            const std::string_view key = token.substr(0u, equals);
            const std::string_view value = token.substr(equals + 1u);
            uint32_t parsedValue = 0u;

            if (key == "particle" && ParseUnsigned(value, parsedValue))
            {
                state.HasParticle = true;
                state.ParticleIndex = parsedValue;
            }
            else if (key == "triangle" && ParseUnsigned(value, parsedValue))
            {
                state.HasTriangle = true;
                state.TriangleIndex = parsedValue;
            }
        }

        if (ampersand == std::string_view::npos)
        {
            break;
        }
        position = ampersand + 1u;
    }

    return state;
}

std::string ReadFileBinary(const std::filesystem::path& filePath)
{
    std::ifstream stream(filePath, std::ios::binary);
    if (stream.is_open() == false)
    {
        return {};
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

#ifdef _WIN32
void SendResponse(
    SOCKET clientSocket,
    int statusCode,
    const char* statusText,
    const std::string& contentType,
    const std::string& body)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Cache-Control: no-store, no-cache, must-revalidate\r\n"
             << "Connection: close\r\n\r\n"
             << body;

    const std::string responseText = response.str();
    std::size_t sentTotal = 0u;

    while (sentTotal < responseText.size())
    {
        const int sent = send(
            clientSocket,
            responseText.data() + sentTotal,
            static_cast<int>(responseText.size() - sentTotal),
            0);
        if (sent <= 0)
        {
            break;
        }
        sentTotal += static_cast<std::size_t>(sent);
    }
}
#endif
} // namespace

BrowserDebugServer& BrowserDebugServer::Get()
{
    static BrowserDebugServer instance{};
    return instance;
}

BrowserDebugServer::~BrowserDebugServer()
{
    Stop();
}

bool BrowserDebugServer::Start(const std::filesystem::path& rootDirectory, uint16_t port)
{
#ifdef _WIN32
    if (m_Running.load())
    {
        return true;
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[BrowserDebugServer] WSAStartupに失敗しました。\n";
        return false;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        WSACleanup();
        std::cerr << "[BrowserDebugServer] socket()に失敗しました。\n";
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        closesocket(listenSocket);
        WSACleanup();
        std::cerr << "[BrowserDebugServer] 127.0.0.1:" << port << " へのbindに失敗しました。\n";
        return false;
    }

    if (listen(listenSocket, 8) == SOCKET_ERROR)
    {
        closesocket(listenSocket);
        WSACleanup();
        std::cerr << "[BrowserDebugServer] listen()に失敗しました。\n";
        return false;
    }

    m_RootDirectory = std::filesystem::absolute(rootDirectory);
    m_Port = port;
    m_ListenSocketValue = static_cast<uintptr_t>(listenSocket);
    m_Running.store(true);
    m_ServerThread = std::thread(&BrowserDebugServer::ServerThreadMain, this);
    return true;
#else
    static_cast<void>(rootDirectory);
    static_cast<void>(port);
    return false;
#endif
}

void BrowserDebugServer::Stop()
{
#ifdef _WIN32
    if (m_Running.exchange(false) == false)
    {
        return;
    }

    const SOCKET listenSocket = static_cast<SOCKET>(m_ListenSocketValue);
    if (m_ListenSocketValue != InvalidSocketValue && listenSocket != INVALID_SOCKET)
    {
        shutdown(listenSocket, SD_BOTH);
        closesocket(listenSocket);
    }

    if (m_ServerThread.joinable())
    {
        m_ServerThread.join();
    }

    m_ListenSocketValue = InvalidSocketValue;
    WSACleanup();
#endif
}

std::string BrowserDebugServer::GetViewerUrl() const
{
    return "http://127.0.0.1:" + std::to_string(m_Port) + "/Viewer.html";
}

BrowserDebugFilterState BrowserDebugServer::GetFilterState() const
{
    std::lock_guard<std::mutex> lock(m_FilterMutex);
    return m_FilterState;
}

void BrowserDebugServer::SetFilterState(const BrowserDebugFilterState& state)
{
    std::lock_guard<std::mutex> lock(m_FilterMutex);
    m_FilterState = state;
}

void BrowserDebugServer::ServerThreadMain()
{
#ifdef _WIN32
    const SOCKET listenSocket = static_cast<SOCKET>(m_ListenSocketValue);

    while (m_Running.load())
    {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
        {
            if (m_Running.load() == false)
            {
                break;
            }
            continue;
        }

        HandleClient(static_cast<uintptr_t>(clientSocket));
        closesocket(clientSocket);
    }
#endif
}

void BrowserDebugServer::HandleClient(uintptr_t clientSocketValue)
{
#ifdef _WIN32
    const SOCKET clientSocket = static_cast<SOCKET>(clientSocketValue);
    std::array<char, 8192u> requestBuffer{};
    const int received = recv(clientSocket, requestBuffer.data(), static_cast<int>(requestBuffer.size() - 1u), 0);
    if (received <= 0)
    {
        return;
    }

    const std::string_view request(requestBuffer.data(), static_cast<std::size_t>(received));
    const std::size_t firstLineEnd = request.find("\r\n");
    if (firstLineEnd == std::string_view::npos)
    {
        SendResponse(clientSocket, 400, "Bad Request", "text/plain; charset=utf-8", "Bad Request");
        return;
    }

    const std::string_view firstLine = request.substr(0u, firstLineEnd);
    constexpr std::string_view GetPrefix = "GET ";

    // RavenはC++17でビルドしているためstring_view::starts_with()は使用しません。
    // 先頭4文字を明示比較し、同じGET判定をC++17で行います。
    if (firstLine.size() < GetPrefix.size()
        || firstLine.substr(0u, GetPrefix.size()) != GetPrefix)
    {
        SendResponse(clientSocket, 405, "Method Not Allowed", "text/plain; charset=utf-8", "GET only");
        return;
    }

    const std::size_t pathEnd = firstLine.find(' ', GetPrefix.size());
    if (pathEnd == std::string_view::npos)
    {
        SendResponse(clientSocket, 400, "Bad Request", "text/plain; charset=utf-8", "Bad Request");
        return;
    }

    const std::string_view requestTarget = firstLine.substr(GetPrefix.size(), pathEnd - GetPrefix.size());
    const std::size_t question = requestTarget.find('?');
    const std::string_view path = question == std::string_view::npos
        ? requestTarget
        : requestTarget.substr(0u, question);
    const std::string_view query = question == std::string_view::npos
        ? std::string_view{}
        : requestTarget.substr(question + 1u);

    if (path == "/filter")
    {
        SetFilterState(ParseFilterQuery(query));
        SendResponse(clientSocket, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
        return;
    }

    std::filesystem::path filePath;
    if (path == "/" || path == "/Viewer.html")
    {
        filePath = m_RootDirectory / "Viewer.html";
    }
    else if (path == "/Startup.svg")
    {
        filePath = m_RootDirectory / "Startup.svg";
    }
    else if (path == "/CandidateRejects.svg")
    {
        filePath = m_RootDirectory / "CandidateRejects.svg";
    }
    else
    {
        SendResponse(clientSocket, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
        return;
    }

    const std::string body = ReadFileBinary(filePath);
    if (body.empty() == true && std::filesystem::exists(filePath) == false)
    {
        SendResponse(clientSocket, 404, "Not Found", "text/plain; charset=utf-8", "Not Found");
        return;
    }

    SendResponse(clientSocket, 200, "OK", GetContentType(filePath), body);
#else
    static_cast<void>(clientSocketValue);
#endif
}

} // namespace Raven
