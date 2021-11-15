#pragma once
#include "response.h"
#include <string>
#include <functional>
#include "mongooseserver.h"
#include <thread>
#include <queue>
#include <mutex>
#include "namedtype.h"
#include <map>


class WebSocketClientImpl
{
    public:
        WebSocketClientImpl(std::function<bool(const url& theUrl)> pConnectCallback, std::function<bool(const url& theUrl, const std::string&)> pMessageCallback, unsigned int nTimeout=250);
        ~WebSocketClientImpl();

        bool Run();
        void Stop();

        bool Connect(const url& theUrl);

        bool SendMessage(const url& theUrl, const std::string& sMessage);

        void CloseConnection(const url& theUrl);

        void Callback(mg_connection* pConnection, int nEvent, void * pEventData);

    private:

        void Loop();

        void SendMessages();

        void CloseConnection(mg_connection* pConnection, bool bTellServer);

        url FindUrl(mg_connection* pConnection);

        mg_mgr m_mgr;

        std::function<bool(const url& theUrl)> m_pConnectCallback;
        std::function<bool(const url& theUrl, const std::string&)> m_pMessageCallback;
        unsigned int m_nTimeout;

        std::unique_ptr<std::thread> m_pThread;
        std::atomic<bool> m_bRun;

        std::mutex m_mutex;

        struct connection
        {
            connection(mg_connection* pc) : pConnection(pc){}
            mg_connection* pConnection;
            std::queue<std::string> q;
        };

        std::map<url, connection> m_mConnection;
};
