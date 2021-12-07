#pragma once
#include <string>
#include "namedtype.h"
#include <functional>
#include <memory>
#include "response.h"


class WebSocketClientImpl;

class RG_EXPORT WebSocketClient
{
    public:
        WebSocketClient(std::function<bool(const endpoint& theEndpoint)> pConnectCallback, std::function<bool(const endpoint& theEndpoint, const std::string&)> pMessageCallback, unsigned int nTimeout=250);
        ~WebSocketClient();

        bool Run();
        void Stop();

        bool Connect(const endpoint& theEndpoint);
        bool SendMessage(const endpoint& theEndpoint, const std::string& sMessage);
        void CloseConnection(const endpoint& theEndpoint);

    private:
        std::unique_ptr<WebSocketClientImpl> m_pImpl;
};
