#include "Slate/Editor/AutomationServer.h"
#include "Slate/Core/Log.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketType = SOCKET;
static constexpr SocketType kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
using SocketType = int;
static constexpr SocketType kInvalidSocket = -1;
#endif

namespace Slate {

namespace {

void CloseSocket(SocketType socket)
{
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool SetNonBlocking(SocketType socket)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}

bool WouldBlock()
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

} // namespace

AutomationServer::~AutomationServer()
{
    Stop();
}

bool AutomationServer::Start(uint16_t port, Handler handler)
{
    if (m_ListenSocket >= 0)
        return true;

#if defined(_WIN32)
    WSADATA data {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        SLATE_ERROR("Automation server: WSAStartup failed");
        return false;
    }
#endif

    SocketType listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kInvalidSocket)
    {
        SLATE_ERROR("Automation server: socket creation failed");
        return false;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        SLATE_ERROR("Automation server: bind failed on port {}", port);
        CloseSocket(listener);
        return false;
    }

    if (listen(listener, 8) != 0)
    {
        SLATE_ERROR("Automation server: listen failed");
        CloseSocket(listener);
        return false;
    }

    SetNonBlocking(listener);

    sockaddr_in bound {};
    socklen_t length = sizeof(bound);
    getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &length);
    m_Port = ntohs(bound.sin_port);

    m_ListenSocket = static_cast<intptr_t>(listener);
    m_Handler = std::move(handler);

    SLATE_INFO("Automation server listening on 127.0.0.1:{}", m_Port);
    return true;
}

void AutomationServer::Stop()
{
    if (m_ListenSocket < 0)
        return;

    for (Client& client : m_Clients)
        CloseSocket(static_cast<SocketType>(client.Socket));
    m_Clients.clear();

    CloseSocket(static_cast<SocketType>(m_ListenSocket));
    m_ListenSocket = -1;
    m_Handler = nullptr;

#if defined(_WIN32)
    WSACleanup();
#endif
}

void AutomationServer::Poll()
{
    if (m_ListenSocket < 0)
        return;

    // Accept pending connections.
    for (;;)
    {
        const SocketType listener = static_cast<SocketType>(m_ListenSocket);
        const SocketType client = accept(listener, nullptr, nullptr);
        if (client == kInvalidSocket)
            break;

        SetNonBlocking(client);
        m_Clients.push_back(Client { static_cast<intptr_t>(client), {} });

        if (m_Clients.size() >= 8)
            break;
    }

    for (auto it = m_Clients.begin(); it != m_Clients.end();)
    {
        const SocketType client = static_cast<SocketType>(it->Socket);

        char buffer[4096];
        const int received = recv(client, buffer, sizeof(buffer), 0);

        if (received > 0)
        {
            it->Buffer.append(buffer, static_cast<size_t>(received));
            ProcessBuffer(*it);
            ++it;
        }
        else if (received == 0)
        {
            CloseSocket(client);
            it = m_Clients.erase(it);
        }
        else if (WouldBlock())
        {
            ++it;
        }
        else
        {
            CloseSocket(client);
            it = m_Clients.erase(it);
        }
    }
}

void AutomationServer::ProcessBuffer(Client& client)
{
    const SocketType socket = static_cast<SocketType>(client.Socket);

    size_t newline = 0;
    while ((newline = client.Buffer.find('\n')) != std::string::npos)
    {
        const std::string line = client.Buffer.substr(0, newline);
        client.Buffer.erase(0, newline + 1);

        if (line.empty())
            continue;

        nlohmann::json response;

        const nlohmann::json request = nlohmann::json::parse(line, nullptr, false);
        if (request.is_discarded())
        {
            response = { { "ok", false }, { "error", "invalid JSON" } };
        }
        else if (m_Handler)
        {
            try
            {
                response = m_Handler(request);
            }
            catch (const std::exception& exception)
            {
                response = { { "ok", false }, { "error", exception.what() } };
            }
        }
        else
        {
            response = { { "ok", false }, { "error", "no handler" } };
        }

        const std::string payload = response.dump() + "\n";
        send(socket, payload.c_str(), static_cast<int>(payload.size()), 0);
    }
}

} // namespace Slate
