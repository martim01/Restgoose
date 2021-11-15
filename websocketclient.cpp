#include "websocketclient.h"
#include "websocketclientimpl.h"

WebSocketClient::WebSocketClient(const url& theUrl, std::function<bool(const url& theUrl)> pConnectCallback, std::function<bool(const url& theUrl, const std::string&)> pMessageCallback, unsigned int nTimeout) :
    m_pImpl(std::make_unique<WebSocketClientImpl>(theUrl, pConnectCallback, pMessageCallback, nTimeout))
{

}

WebSocketClient::~WebSocketClient()
{

}

bool WebSocketClient::Run()
{
    m_pImpl->Run();
}

void WebSocketClient::Stop()
{
    m_pImpl->Stop();
}

void WebSocketClient::SendMessage(const std::string& sMessage)
{
    m_pImpl->SendMessage(sMessage);
}
