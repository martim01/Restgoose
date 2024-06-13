#include "websocketclientimpl.h"
#include "mongoose.h"
#include "log.h"
#include "mongooseserver.h"

using namespace pml::restgoose;


static void callback(struct mg_connection* pConnection, int nEvent, void* pEventData)
{
    reinterpret_cast<WebSocketClientImpl*>(pConnection->fn_data)->Callback(pConnection, nEvent, pEventData);
}


WebSocketClientImpl::WebSocketClientImpl(const std::function<bool(const endpoint& theEndpoint, bool)>& pConnectCallback, 
                                         const std::function<bool(const endpoint& theEndpoint, const std::string&)>& pMessageCallback, unsigned int nTimeout, bool bPingPong) :
    m_pConnectCallback(pConnectCallback),
    m_pMessageCallback(pMessageCallback),
    m_nTimeout(nTimeout)
{
    mg_mgr_init(&m_mgr);        // Initialise event manager
    mg_wakeup_init(&m_mgr);

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
            CheckConnections();
        }
    }
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "websocketclient loop exited";
}

void WebSocketClientImpl::CheckConnections()
{
    //check for any connections that have timed out
    auto now = std::chrono::system_clock::now();

    for(auto itConnection = m_mConnection.begin(); itConnection != m_mConnection.end(); )
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now-itConnection->second.tp).count();

        if(itConnection->second.bConnected == false && elapsed > 3000)
        {
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\tWebsocket connection timeout ";
            if(m_pConnectCallback)
            {
                m_pConnectCallback(itConnection->first, false);
            }

            itConnection->second.pConnection->is_closing = 1;

            auto itErase = itConnection;
            ++itConnection;
            m_mConnection.erase(itErase);
        }
        else if(m_bPingPong&& itConnection->second.bConnected && elapsed > 2000)
        {
            itConnection->second.tp = std::chrono::system_clock::now();
            if(itConnection->second.bPonged == false)    //not replied within the last second
            {
                pmlLog(pml::LOG_WARN, "pml::restgoose") << "Websocket has not responded to PING. Close " << itConnection->second.bPonged;
                itConnection->second.pConnection->is_closing = 1;

                if(m_pConnectCallback)
                {
                    m_pConnectCallback(itConnection->first, false);
                }
                auto itErase = itConnection;
                ++itConnection;
                m_mConnection.erase(itErase);
            }
            else
            {
                mg_ws_send(itConnection->second.pConnection, "hi", 2, WEBSOCKET_OP_PING);
                itConnection->second.bPonged = false;
                ++itConnection;
            }
        }
        else
        {
            ++itConnection;
        }
    }
}

void WebSocketClientImpl::SendMessages()
{
    std::scoped_lock lg(m_mutex);
    for(auto& [theEndpoint, theConnection] : m_mConnection)
    {
        while(theConnection.q.empty() == false)
        {
            mg_ws_send(theConnection.pConnection, theConnection.q.front().c_str(), theConnection.q.front().size(), WEBSOCKET_OP_TEXT);
            theConnection.q.pop();
        }
    }
}


void WebSocketClientImpl::Callback(mg_connection* pConnection, int nEvent, void * pEventData)
{

    switch(nEvent)
    {

        case MG_EV_OPEN:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\tWebsocket connection created";
            break;
        case MG_EV_RESOLVE:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\tWebsocket hostname resolved";
            break;
        case MG_EV_ACCEPT:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\tWebsocket connection accepted";
            break;
        case MG_EV_HTTP_MSG:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\tWebsocket http message!";
            break;
        case MG_EV_CONNECT:
            HandleInitialConnection(pConnection);
            break;
        case MG_EV_ERROR:
            pmlLog(pml::LOG_INFO, "pml::restgoose") << "RestGoose:WebsocketClient\tWebsocket error: " << std::string((char*)pEventData);
            MarkConnectionConnected(pConnection, false);
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\tWebsocket connected " << GetNumberOfConnections(m_mgr);
            MarkConnectionConnected(pConnection, true);
            break;
        case MG_EV_WS_MSG:
            {
                auto pMessage = reinterpret_cast<mg_ws_message*>(pEventData);

                if(m_pMessageCallback && m_bRun)
                {
                    std::string sMessage(pMessage->data.buf, pMessage->data.len);
                    if(m_pMessageCallback(FindUrl(pConnection), sMessage) == false)
                    {
                        CloseConnection(pConnection, true);
                    }
                }
            }
            break;
        case MG_EV_WS_CTL:
            if(m_pConnectCallback)
            {
                auto * pMessage = reinterpret_cast<mg_ws_message*>(pEventData);
                std::string sMessage(pMessage->data.buf, pMessage->data.len);

                if((pMessage->flags & 15) == WEBSOCKET_OP_CLOSE)
                {
                    pmlLog(pml::LOG_WARN, "pml::restgoose") << "Websocket closed by server";
                    m_pConnectCallback(FindUrl(pConnection), false);
                    EraseConnection(pConnection);
                }
                else
                {
                    CheckPong(pConnection, pMessage);
                }
            }
            break;
        case MG_EV_CLOSE:
            pmlLog(pml::LOG_WARN, "pml::restgoose") << "Websocket receieved a close event";
            if(m_pConnectCallback)
            {
                m_pConnectCallback(FindUrl(pConnection), false);
                EraseConnection(pConnection);
            }
            break;
        case MG_EV_WAKEUP:
            SendMessages();
            break;
    }
}

void WebSocketClientImpl::HandleInitialConnection(mg_connection* pConnection)
{
    for(const auto& [theEndpoint, theConnection] : m_mConnection)
    {
        if(theConnection.pConnection == pConnection)
        {
            mg_str host = mg_url_host(theEndpoint.Get().c_str());
            if(mg_url_is_ssl(theEndpoint.Get().c_str()))
            {
                pmlLog(pml::LOG_TRACE, "pml::restgoose") << "WebsocketClient\tConnection with tls";
                mg_tls_opts opts{};
                opts.name = host;
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
        for(auto& [theEndpoint, theConnection] : m_mConnection)
        {
            if(theConnection.pConnection == pConnection)
            {
                theConnection.bPonged  = true;
                break;
            }
        }
    }
}

void WebSocketClientImpl::Stop()
{
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebSocketClientImpl Stop";
    m_bRun = false;
    if(m_pThread)
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebSocketClientImpl Wait on thread";
        m_pThread->join();
        m_pThread = nullptr;
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebSocketClientImpl finished";
    }
}

bool WebSocketClientImpl::SendMessage(const endpoint& theEndpoint, const std::string& sMessage)
{
    m_mutex.lock();
    if(auto itConnection = m_mConnection.find(theEndpoint); itConnection != m_mConnection.end())
    {
        itConnection->second.q.push(sMessage);
        m_mutex.unlock();
        if(m_nPipe != 0)
        {
            mg_wakeup(&m_mgr, m_nPipe, nullptr, 0);
        }
        return true;
    }
    m_mutex.unlock();
    return false;
}


bool WebSocketClientImpl::Connect(const endpoint& theEndpoint)
{
    std::lock_guard lg(m_mutex);
    if(m_mConnection.find(theEndpoint) == m_mConnection.end())
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\t" << "Try to connect to " << theEndpoint;
        auto pConnection = mg_ws_connect(&m_mgr, theEndpoint.Get().c_str(), callback, reinterpret_cast<void*>(this), nullptr);
        if(pConnection)
        {
            m_mConnection.try_emplace(theEndpoint, connection(pConnection));
            return true;
        }
    }
    else
    {
        pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\t" << "Already connected to " << theEndpoint;
    }
    return false;
}


void WebSocketClientImpl::CloseConnection(mg_connection* pConnection, bool bTellServer)
{
    if(bTellServer)
    {
        mg_ws_send(pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
    }
    pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "WebSocketClientImpl::CloseConnection called by client";
    pConnection->is_closing = 1;    //let mongoose know to get rid of the connection

    for(const auto& [theEndpoint, theConnection] : m_mConnection)
    {
        if(theConnection.pConnection == pConnection)
        {
            m_mConnection.erase(theEndpoint);
            break;
        }
    }
}

void WebSocketClientImpl::CloseConnection(const endpoint& theEndpoint)
{
    if(auto itConnection = m_mConnection.find(theEndpoint); itConnection != m_mConnection.end())
    {
        mg_ws_send(itConnection->second.pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
        itConnection->second.pConnection->is_draining = 1; //send everything then close
        m_mConnection.erase(itConnection);
    }
}


endpoint WebSocketClientImpl::FindUrl(mg_connection* pConnection)
{
    for(const auto& [theEndpoint, theConnection] : m_mConnection)
    {
        if(theConnection.pConnection == pConnection)
        {
            return theEndpoint;
        }
    }
    return endpoint("");
}

void WebSocketClientImpl::MarkConnectionConnected(mg_connection* pConnection, bool bConnected)
{
    for(auto& [theEndpoint, theConnection] : m_mConnection)
    {
        if(theConnection.pConnection == pConnection)
        {
            theConnection.bConnected = bConnected;
            if(bConnected)
            {
                theConnection.tp = std::chrono::system_clock::now();    //mark first time connected for future ping/ponging
            }

            if(m_pConnectCallback && m_bRun)
            {
                pmlLog(pml::LOG_DEBUG, "pml::restgoose") << "RestGoose:WebsocketClient\tMarkConnectionConnected  " << bConnected;
                bool bKeep = m_pConnectCallback(theEndpoint, bConnected);
                if(bConnected && bKeep == false)
                {
                    CloseConnection(pConnection, true);
                }
                else if(!bConnected)
                {
                    CloseConnection(pConnection, false);
                }
            }
            break;
        }
    }
}

void WebSocketClientImpl::RemoveCallbacks()
{
    m_pConnectCallback = nullptr;
    m_pMessageCallback = nullptr;
}


void WebSocketClientImpl::EraseConnection(mg_connection* pConnection)
{
    for(const auto& [theEndpoint, theConnection] : m_mConnection)
    {
        if(theConnection.pConnection == pConnection)
        {
            m_mConnection.erase(theEndpoint);
            break;
        }
    }
}

