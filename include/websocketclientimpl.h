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


            private:
                WebSocketClientImpl(std::function<bool(const endpoint& theEndpoint, bool)> pConnectCallback, std::function<bool(const endpoint& theEndpoint, const std::string&)> pMessageCallback, unsigned int nTimeout=250);

                void Loop();

                void MarkConnectionConnected(mg_connection* pConnection, bool bConnected = true);

                void CloseConnection(mg_connection* pConnection, bool bTellServer);

                endpoint FindUrl(mg_connection* pConnection);

                mg_mgr m_mgr;

                std::function<bool(const endpoint& theEndpoint, bool)> m_pConnectCallback;
                std::function<bool(const endpoint& theEndpoint, const std::string&)> m_pMessageCallback;
                unsigned int m_nTimeout;

                std::unique_ptr<std::thread> m_pThread;
                std::atomic<bool> m_bRun;

                std::mutex m_mutex;

                struct connection
                {
                    connection(mg_connection* pc) : pConnection(pc), bConnected(false), tp(std::chrono::system_clock::now()){}
                    mg_connection* pConnection;
                    bool bConnected;
                    std::chrono::time_point<std::chrono::system_clock> tp;
                    std::queue<std::string> q;
                };

                std::map<endpoint, connection> m_mConnection;

                int m_nPipe;
        };
    }
}
