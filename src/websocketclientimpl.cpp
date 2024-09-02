#include "websocketclientimpl.h"
#include "mongoose.h"
#include "log.h"
#include "mongooseserver.h"
#include "threadpool.h"
using namespace pml::restgoose;


static void callback(struct mg_connection* pConnection, int nEvent, void* pEventData)
{
    reinterpret_cast<WebSocketClientImpl*>(pConnection->fn_data)->Callback(pConnection, nEvent, pEventData);
}

static void pipe_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data)
{
    if(nEvent == MG_EV_READ)
    {
        WebSocketClientImpl* pThread = reinterpret_cast<WebSocketClientImpl*>(fn_data);
        pThread->SendMessages();
    }
}


WebSocketClientImpl::WebSocketClientImpl(const std::function<bool(const endpoint& theEndpoint, bool, int)>& pConnectCallback,
                                         const std::function<bool(const endpoint& theEndpoint, const std::string&)>& pMessageCallback, unsigned int nTimeout, bool bPingPong) :
    m_pConnectCallback(pConnectCallback),
    m_pMessageCallback(pMessageCallback),
    m_nTimeout(nTimeout)
{
    mg_mgr_init(&m_mgr);        // Initialise event manager

    m_nPipe = mg_mkpipe(&m_mgr, pipe_handler, reinterpret_cast<void*>(this), true);

}

WebSocketClientImpl::~WebSocketClientImpl()
{
    Stop();

    mg_mgr_free(&m_mgr);
}


bool WebSocketClientImpl::Run()
{
    if(m_pThread == nullptr)
    {
        m_pThread = std::make_unique<std::thread>(&WebSocketClientImpl::Loop, this);
        return true;
    }
    return false;
}

void WebSocketClientImpl::Loop()
{
    while (m_bRun)
    {
        mg_mgr_poll(&m_mgr, m_nTimeout);

        if(m_bRun)
        {
            DoConnect();
            CheckConnections();
        }
    }
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tloop exited";
}

void WebSocketClientImpl::CheckConnections()
{
    //check for any connections that have timed out
    auto now = std::chrono::system_clock::now();

    for(auto itConnection = m_mConnection.begin(); itConnection != m_mConnection.end(); )
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now-itConnection->second.tp);

        if(!CheckToClose(itConnection) && !CheckTimeout(itConnection, elapsed) && !CheckPingPong(itConnection, elapsed))
        {
            ++itConnection;
        }
    }
}

bool WebSocketClientImpl::CheckToClose(std::map<endpoint, connection>::iterator& itConnection)
{
    auto bHandled = false;
    if(itConnection->second.bConnected == true && itConnection->second.bToClose == true)
    {
        mg_ws_send(itConnection->second.pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
        itConnection->second.pConnection->is_closing = 1;
        itConnection = m_mConnection.erase(itConnection);
        bHandled = true;
    }
    return bHandled;

}

bool WebSocketClientImpl::CheckTimeout(std::map<endpoint, connection>::iterator& itConnection, const std::chrono::milliseconds& elapsed)
{
    auto bHandled = false;

    if(itConnection->second.bConnected == false && elapsed.count() > 3000)
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWebsocket connection timeout ";
        if(m_pConnectCallback)
        {
            m_pConnectCallback(itConnection->first, false, enumError::TIMEOUT);
        }

        itConnection->second.pConnection->is_closing = 1;
        itConnection = m_mConnection.erase(itConnection);
        bHandled = true;
    }
    return bHandled;
}

bool WebSocketClientImpl::CheckPingPong(std::map<endpoint, connection>::iterator& itConnection, const std::chrono::milliseconds& elapsed)
{
    auto bHandled = false;

    if(m_bPingPong&& itConnection->second.bConnected && elapsed.count() > 4000)
    {
        itConnection->second.tp = std::chrono::system_clock::now();
        if(itConnection->second.bPonged == false)    //not replied within the last 4 seconds
        {
            pmlLog(pml::LOG_WARN, "pml::restgoose") << "WebsocketClient\tWebsocket has not responded to PING. Close " << itConnection->second.bPonged;
            itConnection->second.pConnection->is_closing = 1;

            if(m_pConnectCallback)
            {
                m_pConnectCallback(itConnection->first, false, enumError::PING);
            }
            itConnection = m_mConnection.erase(itConnection);

            bHandled = true;
        }
        else
        {
            mg_ws_send(itConnection->second.pConnection, "hi", 2, WEBSOCKET_OP_PING);
            itConnection->second.bPonged = false;
        }
    }

    return bHandled;
}


void WebSocketClientImpl::SendMessages()
{
    std::lock_guard<std::mutex> lg(m_mutex);
    for(auto& pairConnection : m_mConnection)
    {
        while(pairConnection.second.q.empty() == false)
        {
            mg_ws_send(pairConnection.second.pConnection, pairConnection.second.q.front().c_str(), pairConnection.second.q.front().size(), WEBSOCKET_OP_TEXT);
            pairConnection.second.q.pop();
        }
    }
}


void WebSocketClientImpl::Callback(mg_connection* pConnection, int nEvent, void * pEventData)
{
    if(nEvent == MG_EV_POLL)
    {
        return;
    }

    char buffer[256];
    mg_snprintf(buffer, sizeof(buffer), "%M", mg_print_ip, &pConnection->rem);

    switch(nEvent)
    {

        case MG_EV_OPEN:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWebsocket connection created" << " connection=" << pConnection->id << "\t" << buffer;
            break;
        case MG_EV_RESOLVE:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWebsocket hostname resolved" << " connection=" << pConnection->id << "\t" << buffer;
            break;
        case MG_EV_ACCEPT:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWebsocket connection accepted" << " connection=" << pConnection->id << "\t" << buffer;
            break;
        case MG_EV_HTTP_MSG:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWebsocket http message!";
            break;
        case MG_EV_CONNECT:
            HandleInitialConnection(pConnection);
            break;
        case MG_EV_ERROR:
            {

                m_mConnectionError[pConnection->id] =  errno;
            }
            pmlLog(pml::LOG_ERROR, "pml::restgoose") << "WebsocketClient\tWebsocket error: "  << " connection=" << pConnection->id << "\t" << std::string((char*)pEventData) << " errno says " << errno << "\t" << buffer;


            MarkConnectionConnected(pConnection, false, errno);
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWebsocket connected " << GetNumberOfConnections(m_mgr);
            MarkConnectionConnected(pConnection);
            break;
        case MG_EV_WS_MSG:
            {
                auto pMessage = reinterpret_cast<mg_ws_message*>(pEventData);

                if(m_pMessageCallback && m_bRun)
                {
                    std::string sMessage(pMessage->data.ptr, pMessage->data.len);
                    if(m_pMessageCallback(FindUrl(pConnection), sMessage) == false)
                    {
                        CloseConnection(pConnection, true);
                    }
                }
            }
            break;
        case MG_EV_WS_CTL:
            CtlEvent(pConnection, pEventData);
            break;
        case MG_EV_CLOSE:
            CloseEvent(pConnection);
            break;
    }
}

void WebSocketClientImpl::CtlEvent(mg_connection* pConnection, void* pEventData)
{
    if(m_pConnectCallback)
    {
        auto * pMessage = reinterpret_cast<mg_ws_message*>(pEventData);
        std::string sMessage(pMessage->data.ptr, pMessage->data.len);

        if((pMessage->flags & 15) == WEBSOCKET_OP_CLOSE)
        {
            pmlLog(pml::LOG_WARN, "pml::restgoose") << "WebsocketClient\tWebsocket closed by server";

            m_pConnectCallback(FindUrl(pConnection), false,  enumError::NONE);
            EraseConnection(pConnection);
        }
        else
        {
            CheckPong(pConnection, pMessage);
        }
    }
}

void WebSocketClientImpl::CloseEvent(mg_connection* pConnection)
{
    int nCode = enumError::NONE;

    auto itError = m_mConnectionError.find(pConnection->id);
    if(itError != m_mConnectionError.end())
    {
        nCode = itError->second;
        m_mConnectionError.erase(itError);

        pmlLog(pml::LOG_WARN, "pml::restgoose") << "WebsocketClient\tWebsocket receieved a close event:" << " connection=" << pConnection->id << "\t" << "Error code found = " << nCode;
    }
    else
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWebsocket receieved a close event: " << " connection=" << pConnection->id;
    }

    auto theEndpoint = FindUrl(pConnection);
    EraseConnection(pConnection);

    if(m_pConnectCallback)
    {
        m_pConnectCallback(theEndpoint, false, nCode);
    }
}

void WebSocketClientImpl::HandleInitialConnection(mg_connection* pConnection) const
{
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "HandleInitialConnection  connection=" << pConnection->id;
    for(const auto& pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            mg_str host = mg_url_host(pairConnection.first.Get().c_str());
            if(mg_url_is_ssl(pairConnection.first.Get().c_str()))
            {
                pmlLog(pml::LOG_TRACE, "pml::restgoose") << "WebsocketClient\tConnection with tls";
                mg_tls_opts opts{};
                opts.srvname = host;
                mg_tls_init(pConnection, &opts);
            }
            break;
        }
    }
}

void WebSocketClientImpl::CheckPong(mg_connection* pConnection, mg_ws_message* pMessage)
{
    if((pMessage->flags & WEBSOCKET_OP_PONG) != 0)
    {
        for(auto& pairConnection: m_mConnection)
        {
            if(pairConnection.second.pConnection == pConnection)
            {
                pairConnection.second.bPonged  = true;
                break;
            }
        }
    }
}

void WebSocketClientImpl::Stop()
{
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tStop";
    m_bRun = false;
    if(m_pThread)
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tWait on thread";
        m_pThread->join();
        m_pThread = nullptr;
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tfinished";
    }
}

bool WebSocketClientImpl::SendMessage(const endpoint& theEndpoint, const std::string& sMessage)
{
    m_mutex.lock();
    auto itConnection = m_mConnection.find(theEndpoint);
    if(itConnection != m_mConnection.end())
    {
        itConnection->second.q.push(sMessage);
        m_mutex.unlock();
        if(m_nPipe != 0)
        {
            send(m_nPipe, "hi", 2, 0);
            //mg_wakeup(&m_mgr, m_nPipe, nullptr, 0);
        }
        return true;
    }
    m_mutex.unlock();
    return false;
}

bool WebSocketClientImpl::DoConnect()
{
    if(m_nQueued == 0)
    {
        return true;
    }

    std::lock_guard<std::mutex> lg(m_mutexConnection);

    if(m_qConnection.empty() == false)
    {
        auto theEndpoint = m_qConnection.front();

        m_qConnection.pop();
        --m_nQueued;

        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\t" << "Try to connect to " << theEndpoint;

        auto pConnection = mg_ws_connect(&m_mgr, theEndpoint.Get().c_str(), callback, reinterpret_cast<void*>(this), nullptr);
        if(pConnection)
        {
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\t" << " Connection created " << pConnection->id << " to " << theEndpoint;
            m_mConnection.insert({theEndpoint, connection(pConnection)});
            return true;
        }
        else
        {
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tFailed to create connection  ";
            if(m_pConnectCallback && m_bRun)
            {
               m_pConnectCallback(theEndpoint, false, enumError::INIT_FAIL);
            }
        }
        return false;
    }
    return true;
}

bool WebSocketClientImpl::Connect(const endpoint& theEndpoint)
{
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\t" << "Connect to " << theEndpoint;
    std::lock_guard<std::mutex> lg(m_mutexConnection);
    if(m_mConnection.find(theEndpoint) == m_mConnection.end())
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\t" << "Connection queued to " << theEndpoint;
        m_qConnection.push(theEndpoint);
        ++m_nQueued;
    }
    else
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\t" << "Already connected to " << theEndpoint;

    }
    return true;
}


void WebSocketClientImpl::CloseConnection(mg_connection* pConnection, bool bTellServer)
{
    if(bTellServer)
    {
        mg_ws_send(pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
    }
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tCloseConnection called by client";
    pConnection->is_closing = 1;    //let mongoose know to get rid of the connection

    for(const auto& pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            m_mConnection.erase(pairConnection.first);
            break;
        }
    }
}

void WebSocketClientImpl::CloseConnection(const endpoint& theEndpoint)
{
    auto itConnection = m_mConnection.find(theEndpoint);
    if(itConnection != m_mConnection.end())
    {
        itConnection->second.bToClose = true;
    }
}


endpoint WebSocketClientImpl::FindUrl(mg_connection* pConnection) const
{
    for(const auto& pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            return pairConnection.first;
        }
    }
    return endpoint("");
}

void WebSocketClientImpl::MarkConnectionConnected(mg_connection* pConnection, bool bConnected, int nCode)
{
    for(auto& pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            auto foundEndpoint = pairConnection.first;
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tEndpoint found " << foundEndpoint;

            pairConnection.second.bConnected = bConnected;
            if(bConnected)
            {
                pairConnection.second.tp = std::chrono::system_clock::now();    //mark first time connected for future ping/ponging
            }
            else if(!bConnected)
            {
                m_mConnection.erase(pairConnection.first);
            }

            if(m_pConnectCallback && m_bRun)
            {
                pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebsocketClient\tTell Callback  " << bConnected << " code = " << nCode;

                bool bKeep = m_pConnectCallback(foundEndpoint, bConnected, nCode);
                if(bConnected && bKeep == false)
                {
                    CloseConnection(pConnection, true);
                }
            }
            break;
        }
    }

    DoConnect();

}

void WebSocketClientImpl::RemoveCallbacks()
{
    m_pConnectCallback = nullptr;
    m_pMessageCallback = nullptr;
}


void WebSocketClientImpl::EraseConnection(mg_connection* pConnection)
{
    for(const auto& pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            m_mConnection.erase(pairConnection.first);
            break;
        }
    }
}
