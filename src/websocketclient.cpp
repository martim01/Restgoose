#include "websocketclient.h"
#include "websocketclientimpl.h"

WebSocketClient::WebSocketClient(std::function<bool(const url& theUrl)> pConnectCallback, std::function<bool(const url& theUrl, const std::string&)> pMessageCallback, unsigned int nTimeout) :
    m_pImpl(std::make_unique<WebSocketClientImpl>(pConnectCallback, pMessageCallback, nTimeout))
{

}

WebSocketClient::~WebSocketClient()
{

}

bool WebSocketClient::Run()
{
    return m_pImpl->Run();
}

void WebSocketClient::Stop()
{
    m_pImpl->Stop();
}

bool WebSocketClient::SendMessage(const url& theUrl, const std::string& sMessage)
{
    return m_pImpl->SendMessage(theUrl, sMessage);
}


bool WebSocketClient::Connect(const url& theUrl)
{
    return m_pImpl->Connect(theUrl);
}

void WebSocketClient::CloseConnection(const url& theUrl)
{
    m_pImpl->CloseConnection(theUrl);
}
