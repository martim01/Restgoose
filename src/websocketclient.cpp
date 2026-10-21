#include "websocketclient.h"

#include "websocketclientimpl.h"
#include "rgutils.h"

namespace pml::restgoose
{

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
    return m_pImpl->SendMessage(theEndpoint, convert_from_json(jsMessage));
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

void WebSocketClient::SetDNS(const std::string& sDNS)
{
    m_pImpl->SetDNS(sDNS);
}

void WebSocketClient::UseProxy(const endpoint& proxy, const std::set<endpoint>& setExcludedEndpoints,
                             const std::map<headerName, headerValue>& proxyHeaders)
{
    m_pImpl->UseProxy(proxy, setExcludedEndpoints, proxyHeaders);
}

void WebSocketClient::IgnoreSSLErrors(bool bIgnore)
{
    m_pImpl->IgnoreSSLErrors(bIgnore);
}

bool WebSocketClient::SetCertificateAuthority(const std::filesystem::path& ca)
{
    return m_pImpl->SetCertificateAuthority(ca);
}

bool WebSocketClient::SetClientCertificate(const std::filesystem::path& cert, const std::filesystem::path& key)
{
    return m_pImpl->SetClientCertificate(cert, key);
}

}