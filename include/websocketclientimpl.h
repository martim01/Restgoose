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
#include <chrono>

namespace pml
{
    namespace restgoose
    {

        class WebSocketClientImpl
        {
            public:
                friend class WebSocketClient;

                ~WebSocketClientImpl();

                bool Run();
                void Stop();

                bool Connect(const endpoint& theEndpoint);

                bool SendMessage(const endpoint& theEndpoint, const std::string& sMessage);

                void CloseConnection(const endpoint& theEndpoint);

                void Callback(mg_connection* pConnection, int nEvent, void * pEventData);

                void SendMessages();

                void RemoveCallbacks();

            private:
                WebSocketClientImpl(const std::function<bool(const endpoint& theEndpoint, bool)>& pConnectCallback, const std::function<bool(const endpoint& theEndpoint, const std::string&)>& pMessageCallback, unsigned int nTimeout=250, bool bPingPong = true);

                void Loop();

                void HandleInitialConnection(mg_connection* pConnection);
                void MarkConnectionConnected(mg_connection* pConnection, bool bConnected = true);

                void CloseConnection(mg_connection* pConnection, bool bTellServer);

                void CheckPong(mg_connection* pConnection, mg_ws_message* pMessage);

                void CheckConnections();

                endpoint FindUrl(mg_connection* pConnection);
                void EraseConnection(mg_connection* pConnection);

                mg_mgr m_mgr;

                std::function<bool(const endpoint& theEndpoint, bool)> m_pConnectCallback;
                std::function<bool(const endpoint& theEndpoint, const std::string&)> m_pMessageCallback;
                unsigned int m_nTimeout;

                std::unique_ptr<std::thread> m_pThread{nullptr};
                std::atomic<bool> m_bRun{true};

                std::mutex m_mutex;

                struct connection
                {
                    explicit connection(mg_connection* pc) : pConnection(pc){}
                    mg_connection* pConnection;
                    bool bConnected = false;
                    bool bPonged = true;
                    std::chrono::time_point<std::chrono::system_clock> tp = std::chrono::system_clock::now();
                    std::queue<std::string> q;
                };

                std::map<endpoint, connection> m_mConnection;

                int m_nPipe{1};
                bool m_bPingPong;
        };
    }
}
