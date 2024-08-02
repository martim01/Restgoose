#include "websocketclient.h"
#include "websocketclientimpl.h"
#include "utils.h"

using namespace pml::restgoose;

WebSocketClient::WebSocketClient(std::function<bool(const endpoint&, bool, int)> pConnectCallback, std::function<bool(const endpoint&, const std::string&)> pMessageCallback, unsigned int nTimeout, bool bPingPong) :
    m_pImpl(std::unique_ptr<WebSocketClientImpl>(new WebSocketClientImpl(pConnectCallback, pMessageCallback, nTimeout, bPingPong)))
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

bool WebSocketClient::Send(const endpoint& theEndpoint, const std::string& sMessage)
{
    return m_pImpl->SendMessage(theEndpoint, sMessage);
}

bool WebSocketClient::Send(const endpoint& theEndpoint, const Json::Value& jsMessage)
{
    return m_pImpl->SendMessage(theEndpoint, ConvertFromJson(jsMessage));
}

bool WebSocketClient::Connect(const endpoint& theEndpoint)
{
    return m_pImpl->Connect(theEndpoint);
}

void WebSocketClient::CloseConnection(const endpoint& theEndpoint)
{
    m_pImpl->CloseConnection(theEndpoint);
}

void WebSocketClient::RemoveCallbacks()
{
    m_pImpl->RemoveCallbacks();
}
