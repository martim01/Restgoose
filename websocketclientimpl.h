#pragma once
#include "response.h"
#include <string>
#include <functional>
#include "mongooseserver.h"
#include <thread>
#include <queue>
#include <mutex>
#include "namedtype.h"

class WebSocketClientImpl
{
    public:
        WebSocketClientImpl(const url& theUrl, std::function<bool(const url& theUrl)> pConnectCallback, std::function<bool(const url& theUrl, const std::string&)> pMessageCallback, unsigned int nTimeout=250);
        ~WebSocketClientImpl();

        bool Run();
        void Stop();

        void SendMessage(const std::string& sMessage);

        void Callback(mg_connection* pConnection, int nEvent, void * pEventData);

    private:

        void Loop();


        void SendMessages(mg_connection* pConnection);

        url m_url;
        std::function<bool(const url& theUrl)> m_pConnectCallback;
        std::function<bool(const url& theUrl, const std::string&)> m_pMessageCallback;
        unsigned int m_nTimeout;

        std::unique_ptr<std::thread> m_pThread;
        std::atomic<bool> m_bRun;

        std::mutex m_mutex;
        std::queue<std::string> m_queue;
};
