#include "websocketclientimpl.h"
#include "mongoose.h"
#include "log.h"

static void callback(struct mg_connection* pConnection, int nEvent, void* pEventData, void* pFnData)
{
    reinterpret_cast<WebSocketClientImpl*>(pFnData)->Callback(pConnection, nEvent, pEventData);
}



WebSocketClientImpl::WebSocketClientImpl(std::function<bool(const url& theUrl)> pConnectCallback, std::function<bool(const url& theUrl, const std::string&)> pMessageCallback, unsigned int nTimeout) :
    m_pConnectCallback(pConnectCallback),
    m_pMessageCallback(pMessageCallback),
    m_nTimeout(nTimeout),
    m_pThread(nullptr),
    m_bRun(true)
{
    mg_mgr_init(&m_mgr);        // Initialise event manager
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
        SendMessages();
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
            pmlLog(pml::LOG_ERROR) << "Websocket error: " << (char*)pEventData;
            CloseConnection(pConnection, false);
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_TRACE) << "Websocket connected";
            if(m_pConnectCallback && m_pConnectCallback(FindUrl(pConnection)) == false)
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

bool WebSocketClientImpl::SendMessage(const url& theUrl, const std::string& sMessage)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    auto itConnection = m_mConnection.find(theUrl);
    if(itConnection != m_mConnection.end())
    {
        itConnection->second.q.push(sMessage);
        return true;
    }
    return false;
}


bool WebSocketClientImpl::Connect(const url& theUrl)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    if(m_mConnection.find(theUrl) == m_mConnection.end())
    {
        auto pConnection = mg_ws_connect(&m_mgr, theUrl.Get().c_str(), callback, reinterpret_cast<void*>(this), nullptr);
        if(pConnection)
        {
            m_mConnection.insert(std::make_pair(theUrl, connection(pConnection)));
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

void WebSocketClientImpl::CloseConnection(const url& theUrl)
{
    auto itConnection = m_mConnection.find(theUrl);
    if(itConnection != m_mConnection.end())
    {
        mg_ws_send(itConnection->second.pConnection, nullptr, 0, WEBSOCKET_OP_CLOSE);
        m_mConnection.erase(itConnection);
    }
}


url WebSocketClientImpl::FindUrl(mg_connection* pConnection)
{
    for(auto pairConnection : m_mConnection)
    {
        if(pairConnection.second.pConnection == pConnection)
        {
            return pairConnection.first;
        }
    }
    return url("");
}
