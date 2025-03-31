#include "RestGoose.h"

#include <iostream>

#include "mongooseserver.h"


namespace pml::restgoose
{

Server::Server() : m_pImpl(std::unique_ptr<MongooseServer>(new MongooseServer()))
{

}

Server::~Server()
{
    m_pImpl->Stop();
}

bool Server::Init(const std::filesystem::path& ca, const std::filesystem::path& cert, const std::filesystem::path& key,const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings)
{
    return m_pImpl->Init(ca, cert, key, addr, nPort, apiRoot, bEnableWebsocket, bSendPings);
}

bool Server::Init(const std::filesystem::path& cert, const std::filesystem::path& key,const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings)
{
    return m_pImpl->Init({}, cert, key, addr, nPort, apiRoot, bEnableWebsocket, bSendPings);
}

bool Server::Init(const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings)
{
    std::cout << "Server::init" << std::endl;
    return m_pImpl->Init({},  {}, {}, addr, nPort, apiRoot, bEnableWebsocket, bSendPings);
}

void Server::SetInterface(const ipAddress& addr, unsigned short nPort)
{
    m_pImpl->SetInterface(addr, nPort);
}

void Server:: Run(bool bThread, const std::chrono::milliseconds& timeout)
{
    m_pImpl->Run(bThread, timeout);
}


void Server::Stop()
{
    m_pImpl->Stop();
}

bool Server::AddEndpoint(const httpMethod& method, const endpoint& theEndpoint, const std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)>& func, bool bUseThread)
{
    return m_pImpl->AddEndpoint(methodpoint(method, theEndpoint), func, bUseThread);
}

void Server::AddNotFoundCallback(const std::function<response(const httpMethod&, const query&, const std::vector<partData>&, const endpoint&, const userName&)>& func)
{
    return m_pImpl->AddNotFoundCallback(func);
}

bool Server::AddWebsocketEndpoint(const endpoint& theMethodPoint, const std::function<bool(const endpoint&, const query&, const userName&, const ipAddress&)>& funcAuthentication, const std::function<bool(const endpoint&, const Json::Value&)>& funcMessage, const std::function<void(const endpoint&, const ipAddress&)>& funcClose)
{
    return m_pImpl->AddWebsocketEndpoint(theMethodPoint, funcAuthentication, funcMessage, funcClose);
}

bool Server::DeleteEndpoint(const httpMethod& method, const endpoint& theEndpoint)
{
    return m_pImpl->DeleteEndpoint(methodpoint(method, theEndpoint));
}

void Server::SetLoopCallback(const std::function<void(std::chrono::milliseconds)>& func)
{
    m_pImpl->SetLoopCallback(func);
}

void Server::SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage)
{
    m_pImpl->SendWebsocketMessage(setEndpoints, jsMessage);
}

void Server::SetAuthorizationTypeBearer(const std::function<bool(const methodpoint&, const std::string&)>& callback, const std::function<response(const endpoint& uri, bool)>& callbackHandleNotAuthorized, bool bAuthenticateWebsocketsViaQuery)
{
    m_pImpl->SetAuthorizationTypeBearer(callback, callbackHandleNotAuthorized, bAuthenticateWebsocketsViaQuery);
}
void Server::SetAuthorizationTypeBasic(const userName& aUser, const password& aPassword)
{
    m_pImpl->SetAuthorizationTypeBasic(aUser, aPassword);
}

void Server::SetAuthorizationTypeNone()
{
    m_pImpl->SetAuthorizationTypeNone();
}

bool Server::AddBAUser(const userName& aUser, const password& aPassword)
{
    return m_pImpl->AddBAUser(aUser,aPassword);
}

bool Server::DeleteBAUser(const userName& aUser)
{
    return m_pImpl->DeleteBAUser(aUser);
}

std::set<methodpoint> Server::GetEndpoints()
{
    return m_pImpl->GetEndpoints();
}

void Server::SetStaticDirectory(const std::string& sDir)
{
    m_pImpl->SetStaticDirectory(sDir);
}

const std::string& Server::GetStaticDirectory() const
{
    return m_pImpl->GetStaticDirectory();
}

unsigned long Server::GetPort() const
{
    return m_pImpl->GetPort();
}

void Server::Wait()
{
    m_pImpl->Wait();
}

void Server::PrimeWait()
{
    m_pImpl->PrimeWait();
}


void Server::Signal(const response& resp)
{
    m_pImpl->Signal(resp);
}

const response& Server::GetSignalResponse() const
{
    return m_pImpl->GetSignalResponse();
}

void Server::SetMaxConnections(size_t nMax)
{
    m_pImpl->SetMaxConnections(nMax);
}

const ipAddress& Server::GetCurrentPeer(bool bIncludePort) const
{
    return m_pImpl->GetCurrentPeer(bIncludePort);
}

size_t Server::GetNumberOfWebsocketConnections() const
{
    return m_pImpl->GetNumberOfWebsocketConnections();
}

void Server::SetAccessControlList(const std::string& sAcl)
{
    m_pImpl->SetAccessControlList(sAcl);
}

const std::string& Server::GetAccessControlList() const
{
    return m_pImpl->GetAccessControlList();
}

void Server::SetUnprotectedEndpoints(const std::set<methodpoint>& setUnprotected)
{
    m_pImpl->SetUnprotectedEndpoints(setUnprotected);
}

void Server::AddHeaders(const std::map<headerName, headerValue>& mHeaders)
{
    m_pImpl->AddHeaders(mHeaders);
}

void Server::RemoveHeaders(const std::set<headerName>& setHeaders)
{
    m_pImpl->RemoveHeaders(setHeaders);
}

void Server::SetHeaders(const std::map<headerName, headerValue>& mHeaders)
{
    m_pImpl->SetHeaders(mHeaders);
}

}