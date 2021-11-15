#include "websocketclientimpl.h"
#include "mongoose.h"
#include "log.h"

static void callback(struct mg_connection* pConnection, int nEvent, void* pEventData, void* pFnData)
{
    reinterpret_cast<WebSocketClientImpl*>(pFnData)->Callback(pConnection, nEvent, pEventData);
}



WebSocketClientImpl::WebSocketClientImpl(const url& theUrl, std::function<bool(const url& theUrl)> pConnectCallback, std::function<bool(const url& theUrl, const std::string&)> pMessageCallback, unsigned int nTimeout) :
    m_url(theUrl),
    m_pConnectCallback(pConnectCallback),
    m_pMessageCallback(pMessageCallback),
    m_nTimeout(nTimeout),
    m_pThread(nullptr),
    m_bRun(true)
{
}

WebSocketClientImpl::~WebSocketClientImpl()
{
    Stop();
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
    mg_mgr mgr;
    mg_mgr_init(&mgr);        // Initialise event manager

    auto pConnection = mg_ws_connect(&mgr, m_url.Get().c_str(), callback, reinterpret_cast<void*>(this), nullptr);

    while (pConnection && m_bRun)
    {
        mg_mgr_poll(&mgr, m_nTimeout);

        SendMessages(pConnection);
    }
    mg_mgr_free(&mgr);
}


void WebSocketClientImpl::SendMessages(mg_connection* pConnection)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    while(m_queue.empty() == false)
    {
        mg_ws_send(pConnection, m_queue.front().c_str(), m_queue.front().size(), WEBSOCKET_OP_TEXT);
        m_queue.pop();
    }
}


void WebSocketClientImpl::Callback(mg_connection* pConnection, int nEvent, void * pEventData)
{

    switch(nEvent)
    {
        case MG_EV_ERROR:
            pmlLog(pml::LOG_ERROR) << "Websocket error: " << (char*)pEventData;
            m_bRun = false;
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_TRACE) << "Websocket connected";
            if(m_pConnectCallback && m_pConnectCallback(m_url) == false)
            {
                m_bRun = false;
            }
            break;
        case MG_EV_WS_MSG:
            if(m_pMessageCallback && m_pMessageCallback(m_url, (char*)pEventData) == false)
            {
                m_bRun = false;
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

void WebSocketClientImpl::SendMessage(const std::string& sMessage)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    m_queue.push(sMessage);
}
