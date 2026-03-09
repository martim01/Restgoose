#include "httpclient.h"

#include "httpclientimpl.h"
#include "log.h"
#include "threadpool.h"


namespace pml::restgoose{



HttpClient::~HttpClient()=default;

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, mExtraHeaders, eResponse)))  //using new as constructor is private
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const Json::Value& jsData, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, jsData, mExtraHeaders, eResponse)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target,data, contentType, mExtraHeaders, eResponse)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const textData& filename, const std::filesystem::path& filepath, const headerValue& contentType, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, filename, filepath, contentType, mExtraHeaders, eResponse)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse)  :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, vData,  mExtraHeaders, eResponse)))
{

}

const clientResponse& HttpClient::Run(const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout) const
{
    return m_pImpl->Run(connectionTimeout, processTimeout);
}

void HttpClient::Run(const std::function<void(const clientResponse&, unsigned int, const std::string& )>& pCallback, unsigned int nRunId, const std::string& sUser, const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout, const std::chrono::milliseconds& delay) const
{

    ThreadPool::Get().Submit([=, pImpl=m_pImpl]
                             {
                                pml::log::log(pml::log::Level::kTrace, "pml::restgoose") << "HttpClient::Run #" << nRunId;
                                std::this_thread::sleep_for(delay);
                                pml::log::log(pml::log::Level::kTrace, "pml::restgoose") << "HttpClient::RunAsync " << nRunId;
                                pImpl->RunAsync(pCallback, nRunId, sUser, connectionTimeout, processTimeout);
                            });
}

void HttpClient::Run(const std::function<void(const clientResponse&, unsigned int)>& pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout, const std::chrono::milliseconds& delay) const
{

    ThreadPool::Get().Submit([=, pImpl=m_pImpl]
                             {
                                pml::log::log(pml::log::Level::kTrace, "pml::restgoose") << "HttpClient::Run #" << nRunId;
                                std::this_thread::sleep_for(delay);
                                pml::log::log(pml::log::Level::kTrace, "pml::restgoose") << "HttpClient::RunAsync " << nRunId;
                                pImpl->RunAsyncOld(pCallback, nRunId, connectionTimeout, processTimeout);
                            });
}

void HttpClient::SetUploadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback) const
{
    m_pImpl->SetUploadProgressCallback(pCallback);
}

void HttpClient::SetDownloadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback) const
{
    m_pImpl->SetDownloadProgressCallback(pCallback);
}

void HttpClient::Cancel() const
{
    m_pImpl->Cancel();
}

bool HttpClient::SetBasicAuthentication(const userName& user, const password& pass) const
{
    return m_pImpl->SetBasicAuthentication(user, pass);
}

bool HttpClient::SetBearerAuthentication(const std::string& sToken) const
{
    return m_pImpl->SetBearerAuthentication(sToken);
}

bool HttpClient::SetCertificateAuthority(const std::filesystem::path& ca) const
{
    return m_pImpl->SetCertificateAuthority(ca);
}

bool HttpClient::SetClientCertificate(const std::filesystem::path& cert, const std::filesystem::path& key) const
{
    return m_pImpl->SetClientCertificate(cert, key);
}

bool HttpClient::SetMethod(const httpMethod& method) const
{
    return m_pImpl->SetMethod(method);
}

bool HttpClient::SetEndpoint(const endpoint& target) const
{
    return m_pImpl->SetEndpoint(target);
}

bool HttpClient::SetData(const Json::Value& jsData) const
{
    return m_pImpl->SetData(jsData);
}

bool HttpClient::SetData(const textData& data) const
{
    return m_pImpl->SetData(data);
}

bool HttpClient::SetFile(const textData& filename, const std::filesystem::path& filepath) const
{
    return m_pImpl->SetFile(filename, filepath);
}

bool HttpClient::SetPartData(const std::vector<partData>& vData) const
{
    return m_pImpl->SetPartData(vData);
}

bool HttpClient::AddHeaders(const std::map<headerName, headerValue>& mHeaders) const
{
    return m_pImpl->AddHeaders(mHeaders);
}

bool HttpClient::SetExpectedResponse(const clientResponse::enumResponse eResponse) const
{
    return m_pImpl->SetExpectedResponse(eResponse);
}


void HttpClient::UseProxy(const std::string& proxy)
{
    m_pImpl->UseProxy(endpoint(proxy));
}

}