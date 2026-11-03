#pragma once

#include "Slate/Core/Base.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Slate {

// Minimal JSON-over-TCP control server used to drive the editor from AI agents
// or external tools. Requests are newline-delimited JSON objects; each receives
// one newline-delimited JSON response. The server is polled on the main thread,
// so handlers can safely touch the scene and renderer.
class AutomationServer
{
public:
    using Handler = std::function<nlohmann::json(const nlohmann::json&)>;

    AutomationServer() = default;
    ~AutomationServer();

    AutomationServer(const AutomationServer&) = delete;
    AutomationServer& operator=(const AutomationServer&) = delete;

    bool Start(uint16_t port, Handler handler);
    void Stop();
    void Poll();

    bool IsRunning() const { return m_ListenSocket >= 0; }
    uint16_t GetPort() const { return m_Port; }

private:
    struct Client
    {
        intptr_t Socket = -1;
        std::string Buffer;
    };

    void ProcessBuffer(Client& client);

    intptr_t m_ListenSocket = -1;
    std::vector<Client> m_Clients;
    Handler m_Handler;
    uint16_t m_Port = 0;
};

} // namespace Slate
