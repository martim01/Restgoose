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
#include <atomic>

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
                enum enumError {NONE=0, INIT_FAIL=-1, TIMEOUT=-2, PING=-3};

                WebSocketClientImpl(const std::function<bool(const endpoint& theEndpoint, bool, int)>& pConnectCallback, const std::function<bool(const endpoint& theEndpoint, const std::string&)>& pMessageCallback, unsigned int nTimeout=250, bool bPingPong = true);

                void Loop();

                void HandleInitialConnection(mg_connection* pConnection) const;
                void MarkConnectionConnected(mg_connection* pConnection, bool bConnected = true, int code=0);

                void CloseConnection(mg_connection* pConnection, bool bTellServer);

                void CheckPong(mg_connection* pConnection, mg_ws_message* pMessage);

                void CheckConnections();

                void CloseEvent(mg_connection* pConnection);
                void CtlEvent(mg_connection* pConnection, void* pEventData);

                endpoint FindUrl(mg_connection* pConnection) const;
                void EraseConnection(mg_connection* pConnection);

                bool DoConnect();

                mg_mgr m_mgr;

                std::function<bool(const endpoint& theEndpoint, bool, int)> m_pConnectCallback;
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
                    bool bToClose = false;
                    std::chrono::time_point<std::chrono::system_clock> tp = std::chrono::system_clock::now();
                    std::queue<std::string> q;
                };

                bool CheckToClose(std::map<endpoint, connection>::iterator& itConnection);
                bool CheckTimeout(std::map<endpoint, connection>::iterator& itConnection, const std::chrono::milliseconds& elapsed);
                bool CheckPingPong(std::map<endpoint, connection>::iterator& itConnection, const std::chrono::milliseconds& elapsed);


                std::map<endpoint, connection> m_mConnection;


                std::map<unsigned long, int> m_mConnectionError;
                std::mutex m_mutexConnection;

                std::queue<endpoint> m_qConnection;
                std::atomic<size_t> m_nQueued{0};

                int m_nPipe{1};
                bool m_bPingPong;
        };
    }
}
