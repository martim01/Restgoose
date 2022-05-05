#include "websocketclientimpl.h"
#include "mongoose.h"
#include "log.h"
#include "mongooseserver.h"

using namespace pml::restgoose;


static void callback(struct mg_connection* pConnection, int nEvent, void* pEventData, void* pFnData)
{
    reinterpret_cast<WebSocketClientImpl*>(pFnData)->Callback(pConnection, nEvent, pEventData);
}

static void pipe_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data)
{
    if(nEvent == MG_EV_READ)
    {
        WebSocketClientImpl* pThread = reinterpret_cast<WebSocketClientImpl*>(fn_data);
        pThread->SendMessages();
    }
}

WebSocketClientImpl::WebSocketClientImpl(std::function<bool(const endpoint& theEndpoint, bool)> pConnectCallback, std::function<bool(const endpoint& theEndpoint, const std::string&)> pMessageCallback, unsigned int nTimeout) :
    m_pConnectCallback(pConnectCallback),
    m_pMessageCallback(pMessageCallback),
    m_nTimeout(nTimeout),
    m_pThread(nullptr),
    m_bRun(true)
{
    mg_mgr_init(&m_mgr);        // Initialise event manager

    m_nPipe = mg_mkpipe(&m_mgr, pipe_handler, reinterpret_cast<void*>(this));
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

        //check for any connections that have timed out
        auto now = std::chrono::system_clock::now();

        for(auto itConnection = m_mConnection.begin(); itConnection != m_mConnection.end(); )
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now-itConnection->second.tp).count();

            if(itConnection->second.bConnected == false && elapsed > 3000)
            {
                pmlLog(pml::LOG_DEBUG) << "RestGoose:WebsocketClient\tWebsocket connection timeout ";
                if(m_pConnectCallback)
                {
                    m_pConnectCallback(itConnection->first, false);
                }

                itConnection->second.pConnection->is_closing = 1;

                auto itErase = itConnection;
                ++itConnection;
                m_mConnection.erase(itErase);
            }
            else if(itConnection->second.bConnected && elapsed > 2000)
            {
                itConnection->second.tp = std::chrono::system_clock::now();
                if(itConnection->second.bPonged == false)    //not replied within the last second
                {
                    pmlLog(pml::LOG_WARN) << "Websocket has not responded to PING. Close " << itConnection->second.bPonged;
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

    switch(nEvent)
    {
        case MG_EV_CONNECT:
            HandleInitialConnection(pConnection);
            break;
        case MG_EV_ERROR:
            pmlLog(pml::LOG_INFO) << "RestGoose:WebsocketClient\tWebsocket error: " << (char*)pEventData;
            MarkConnectionConnected(pConnection, false);
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_DEBUG) << "RestGoose:WebsocketClient\tWebsocket connected " << GetNumberOfConnections(m_mgr);
            MarkConnectionConnected(pConnection, true);
            break;
        case MG_EV_WS_MSG:
            {
                mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pEventData);
                //CheckPong(pConnection, pMessage);

                if(m_pMessageCallback)
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
            if(m_pConnectCallback)
            {
                mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pEventData);
                std::string sMessage(pMessage->data.ptr, pMessage->data.len);

                if((pMessage->flags & 15) == WEBSOCKET_OP_CLOSE)
                {
                    pmlLog() << "Websocket closed by server";
                    m_pConnectCallback(FindUrl(pConnection), false);
                }
                CheckPong(pConnection, pMessage);
            }
            break;
    }
}

void WebSocketClientImpl::HandleInitialConnection(mg_connection* pConnection)
{
    for(auto& pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            mg_str host = mg_url_host(pairConnection.first.Get().c_str());
            if(mg_url_is_ssl(pairConnection.first.Get().c_str()))
            {
                pmlLog(pml::LOG_TRACE) << "WebsocketClient\tConnection is wss";
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
        for(auto& pairConnection : m_mConnection)
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
    if(m_pThread)
    {
        m_bRun = false;
        m_pThread->join();
        m_pThread = nullptr;
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
            //mg_mgr_wakeup(m_pPipe, nullptr, 0);
        }
        return true;
    }
    m_mutex.unlock();
    return false;
}


bool WebSocketClientImpl::Connect(const endpoint& theEndpoint)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    if(m_mConnection.find(theEndpoint) == m_mConnection.end())
    {
        pmlLog(pml::LOG_DEBUG) << "RestGoose:WebsocketClient\t" << "Try to connect to " << theEndpoint;
        auto pConnection = mg_ws_connect(&m_mgr, theEndpoint.Get().c_str(), callback, reinterpret_cast<void*>(this), nullptr);
        if(pConnection)
        {
            m_mConnection.insert(std::make_pair(theEndpoint, connection(pConnection)));
            return true;
        }
    }
    else
    {
        pmlLog(pml::LOG_DEBUG) << "RestGoose:WebsocketClient\t" << "Already connected to " << theEndpoint;
    }
    return false;
}


void WebSocketClientImpl::CloseConnection(mg_connection* pConnection, bool bTellServer)
{
    if(bTellServer)
    {
        mg_ws_send(pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
    }
    pmlLog() << "WebSocketClientImpl::CloseConnection called by client";
    pConnection->is_closing = 1;    //let mongoose know to get rid of the connection

    for(auto pairConnection : m_mConnection)
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
        mg_ws_send(itConnection->second.pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
        itConnection->second.pConnection->is_draining = 1; //send everything then close
        m_mConnection.erase(itConnection);
    }
}


endpoint WebSocketClientImpl::FindUrl(mg_connection* pConnection)
{
    for(auto pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            return pairConnection.first;
        }
    }
    return endpoint("");
}

void WebSocketClientImpl::MarkConnectionConnected(mg_connection* pConnection, bool bConnected)
{
    for(auto& pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            pairConnection.second.bConnected = bConnected;
            if(bConnected)
            {
                pairConnection.second.tp = std::chrono::system_clock::now();    //mark first time connected for future ping/ponging
            }

            if(m_pConnectCallback)
            {
                pmlLog(pml::LOG_DEBUG) << "RestGoose:WebsocketClient\tMarkConnectionConnected  " << bConnected;
                bool bKeep = m_pConnectCallback(pairConnection.first, bConnected);
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


