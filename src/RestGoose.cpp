#include "RestGoose.h"
#include "mongooseserver.h"

using namespace pml::restgoose;




Server::Server() : m_pImpl(std::unique_ptr<MongooseServer>(new MongooseServer()))
{

}

Server::~Server()
{
    m_pImpl->Stop();
}

bool Server::Init(const std::string& sCert, const std::string& sKey, int nPort, const std::string& sRootApi, bool bEnableWebsocket)
{
    return m_pImpl->Init(sCert, sKey, nPort, sRootApi, bEnableWebsocket);
}

void Server:: Run(bool bThread, unsigned int nTimeoutMs)
{
    m_pImpl->Run(bThread, nTimeoutMs);
}


void Server::Stop()
{
    m_pImpl->Stop();
}

bool Server::AddEndpoint(const methodpoint& theMethodPoint, std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)> func)
{
    return m_pImpl->AddEndpoint(theMethodPoint, func);
}

void Server::AddNotFoundCallback(std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)> func)
{
    return m_pImpl->AddNotFoundCallback(func);
}

bool Server::AddWebsocketEndpoint(const endpoint& theMethodPoint, std::function<bool(const endpoint&, const userName&, const ipAddress&)> funcAuthentication, std::function<bool(const endpoint&, const Json::Value&)> funcMessage, std::function<void(const endpoint&, const ipAddress&)> funcClose)
{
    return m_pImpl->AddWebsocketEndpoint(theMethodPoint, funcAuthentication, funcMessage, funcClose);
}

bool Server::DeleteEndpoint(const methodpoint& theMethodPoint)
{
    return m_pImpl->DeleteEndpoint(theMethodPoint);
}

void Server::SetLoopCallback(std::function<void(unsigned int)> func)
{
    m_pImpl->SetLoopCallback(func);
}

void Server::SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage)
{
    m_pImpl->SendWebsocketMessage(setEndpoints, jsMessage);
}


void Server::AddBAUser(const userName& aUser, const password& aPassword)
{
    m_pImpl->AddBAUser(aUser,aPassword);
}

void Server::DeleteBAUser(const userName& aUser)
{
    m_pImpl->DeleteBAUser(aUser);
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

bool Server::IsOk()
{
    return m_pImpl->IsOk();
}

void Server::Signal(bool bOk, const std::string& sData)
{
    m_pImpl->Signal(bOk, sData);
}

const std::string& Server::GetSignalData()
{
    return m_pImpl->GetSignalData();
}
