#include "websocketclient.h"
#include "websocketclientimpl.h"

WebSocketClient::WebSocketClient(std::function<bool(const endpoint& theEndpoint)> pConnectCallback, std::function<bool(const endpoint& theEndpoint, const std::string&)> pMessageCallback, unsigned int nTimeout) :
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

bool WebSocketClient::SendMessage(const endpoint& theEndpoint, const std::string& sMessage)
{
    return m_pImpl->SendMessage(theEndpoint, sMessage);
}


bool WebSocketClient::Connect(const endpoint& theEndpoint)
{
    return m_pImpl->Connect(theEndpoint);
}

void WebSocketClient::CloseConnection(const endpoint& theEndpoint)
{
    m_pImpl->CloseConnection(theEndpoint);
}
