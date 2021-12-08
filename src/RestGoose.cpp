#include "RestGoose.h"
#include "mongooseserver.h"


const httpMethod RestGoose::GET    = httpMethod("GET");
const httpMethod RestGoose::POST   = httpMethod("POST");
const httpMethod RestGoose::PUT    = httpMethod("PUT");
const httpMethod RestGoose::PATCH  = httpMethod("PATCH");
const httpMethod RestGoose::HTTP_DELETE = httpMethod("DELETE");
const httpMethod RestGoose::OPTIONS = httpMethod("OPTIONS");


RestGoose::RestGoose() : m_pImpl(std::make_unique<MongooseServer>())
{

}

RestGoose::~RestGoose()
{
    m_pImpl->Stop();
}

bool RestGoose::Init(const std::string& sCert, const std::string& sKey, int nPort, const std::string& sRootApi, bool bEnableWebsocket)
{
    return m_pImpl->Init(sCert, sKey, nPort, sRootApi, bEnableWebsocket);
}

void RestGoose:: Run(bool bThread, unsigned int nTimeoutMs)
{
    m_pImpl->Run(bThread, nTimeoutMs);
}


void RestGoose::Stop()
{
    m_pImpl->Stop();
}

bool RestGoose::AddEndpoint(const methodpoint& theMethodPoint, std::function<response(const query&, const postData&, const endpoint&, const userName&)> func)
{
    return m_pImpl->AddEndpoint(theMethodPoint, func);
}

void RestGoose::AddNotFoundCallback(std::function<response(const query&, const postData&, const endpoint&, const userName&)> func)
{
    return m_pImpl->AddNotFoundCallback(func);
}

bool RestGoose::AddWebsocketEndpoint(const endpoint& theMethodPoint, std::function<bool(const endpoint&, const userName&, const ipAddress&)> funcAuthentication, std::function<bool(const endpoint&, const Json::Value&)> funcMessage, std::function<void(const endpoint&, const ipAddress&)> funcClose)
{
    return m_pImpl->AddWebsocketEndpoint(theMethodPoint, funcAuthentication, funcMessage, funcClose);
}

bool RestGoose::DeleteEndpoint(const methodpoint& theMethodPoint)
{
    return m_pImpl->DeleteEndpoint(theMethodPoint);
}

void RestGoose::SetLoopCallback(std::function<void(unsigned int)> func)
{
    m_pImpl->SetLoopCallback(func);
}

void RestGoose::SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage)
{
    m_pImpl->SendWebsocketMessage(setEndpoints, jsMessage);
}


void RestGoose::AddBAUser(const userName& aUser, const password& aPassword)
{
    m_pImpl->AddBAUser(aUser,aPassword);
}

void RestGoose::DeleteBAUser(const userName& aUser)
{
    m_pImpl->DeleteBAUser(aUser);
}

std::set<methodpoint> RestGoose::GetEndpoints()
{
    return m_pImpl->GetEndpoints();
}

void RestGoose::SetStaticDirectory(const std::string& sDir)
{
    m_pImpl->SetStaticDirectory(sDir);
}

const std::string& RestGoose::GetStaticDirectory() const
{
    return m_pImpl->GetStaticDirectory();
}

unsigned long RestGoose::GetPort() const
{
    return m_pImpl->GetPort();
}

void RestGoose::Wait()
{
    m_pImpl->Wait();
}

void RestGoose::PrimeWait()
{
    m_pImpl->PrimeWait();
}

bool RestGoose::IsOk()
{
    return m_pImpl->IsOk();
}

void RestGoose::Signal(bool bOk, const std::string& sData)
{
    m_pImpl->Signal(bOk, sData);
}

const std::string& RestGoose::GetSignalData()
{
    return m_pImpl->GetSignalData();
}
