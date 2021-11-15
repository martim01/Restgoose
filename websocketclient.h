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
        WebSocketClient(const url& theUrl, std::function<bool(const url& theUrl)> pConnectCallback, std::function<bool(const url& theUrl, const std::string&)> pMessageCallback, unsigned int nTimeout=250);
        ~WebSocketClient();

        bool Run();
        void Stop();

        void SendMessage(const std::string& sMessage);
    private:
        std::unique_ptr<WebSocketClientImpl> m_pImpl;
};
