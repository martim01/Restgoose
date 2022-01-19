#include "websocketclientimpl.h"
#include "mongoose.h"
#include "log.h"

using namespace pml::restgoose;


static void callback(struct mg_connection* pConnection, int nEvent, void* pEventData, void* pFnData)
{
    reinterpret_cast<WebSocketClientImpl*>(pFnData)->Callback(pConnection, nEvent, pEventData);
}

static void pipe_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_dat)
{
    if(nEvent == MG_EV_READ)
    {
        WebSocketClientImpl* pThread = reinterpret_cast<WebSocketClientImpl*>(pConnection->fn_data);
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

    m_pPipe = mg_mkpipe(&m_mgr, pipe_handler, nullptr);
    if(m_pPipe)
    {
        m_pPipe->fn_data = reinterpret_cast<void*>(this);
    }
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
        //SendMessages();
    }
}


void WebSocketClientImpl::SendMessages()
{
    std::lock_guard<std::mutex> lg(m_mutex);
    for(auto pairConnection : m_mConnection)
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
        case MG_EV_ERROR:
            pmlLog(pml::LOG_ERROR) << "RestGoose:WebsocketClient\tWebsocket error: " << (char*)pEventData;
            CloseConnection(pConnection, false);
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_DEBUG) << "RestGoose:WebsocketClient\tWebsocket connected";
            if(m_pConnectCallback && m_pConnectCallback(FindUrl(pConnection), true) == false)
            {
                CloseConnection(pConnection, true);
            }
            break;
        case MG_EV_WS_MSG:
            if(m_pMessageCallback)
            {
                mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pEventData);
                std::string sMessage(pMessage->data.ptr, pMessage->data.len);
                if(m_pMessageCallback(FindUrl(pConnection), sMessage) == false)
                {
                    CloseConnection(pConnection, true);
                }
            }
            else
            {
                pmlLog(pml::LOG_DEBUG) << "RestGoose:WebsocketClient\tWebsocket message no callback";
            }
            break;
        case MG_EV_WS_CTL:
            if(m_pConnectCallback)
            {
                mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pEventData);
                std::string sMessage(pMessage->data.ptr, pMessage->data.len);
                if((pMessage->flags & 15) == 8)   //this is the close flag apparently
                {
                    m_pConnectCallback(FindUrl(pConnection), false);
                }
            }
            break;
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
        if(m_pPipe)
        {
            mg_mgr_wakeup(m_pPipe, nullptr, 0);
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
        auto pConnection = mg_ws_connect(&m_mgr, theEndpoint.Get().c_str(), callback, reinterpret_cast<void*>(this), nullptr);
        if(pConnection)
        {
            m_mConnection.insert(std::make_pair(theEndpoint, connection(pConnection)));
            return true;
        }
    }
    return false;
}


void WebSocketClientImpl::CloseConnection(mg_connection* pConnection, bool bTellServer)
{
    if(bTellServer)
    {
        mg_ws_send(pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
    }

    for(auto pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            m_mConnection.erase(pairConnection.first);
            break;
        }
    }

    //@todo do we need to close this another way as well??
}

void WebSocketClientImpl::CloseConnection(const endpoint& theEndpoint)
{
    auto itConnection = m_mConnection.find(theEndpoint);
    if(itConnection != m_mConnection.end())
    {
        mg_ws_send(itConnection->second.pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
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
