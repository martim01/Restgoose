#include "httpclient.h"
#include "restfulclient.h"

using namespace pml::restgoose;



HttpClient::~HttpClient() = default;

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue> mExtraHeaders) :
    m_pImpl(std::unique_ptr<HttpClientImpl>(new HttpClientImpl(method, target, mExtraHeaders)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders) :
    m_pImpl(std::unique_ptr<HttpClientImpl>(new HttpClientImpl(method, target,data, contentType, mExtraHeaders)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const textData& filename, const fileLocation& filepath, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders) :
    m_pImpl(std::unique_ptr<HttpClientImpl>(new HttpClientImpl(method, target, filename, filepath, contentType, mExtraHeaders)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue> mExtraHeaders)  :
    m_pImpl(std::unique_ptr<HttpClientImpl>(new HttpClientImpl(method, target, vData,  mExtraHeaders)))
{

}

const clientResponse& HttpClient::Run(const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    return m_pImpl->Run(connectionTimeout, processTimeout);
}

void HttpClient::SetProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback)
{
    m_pImpl->SetProgressCallback(pCallback);
}
