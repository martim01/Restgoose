#include "httpclient.h"
#include "httpclientimpl.h"
#include "threadpool.h"
#include "log.h"

using namespace pml::restgoose;



HttpClient::~HttpClient()
{
}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, mExtraHeaders, eResponse)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const Json::Value& jsData, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, jsData, mExtraHeaders, eResponse)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target,data, contentType, mExtraHeaders, eResponse)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const textData& filename, const fileLocation& filepath, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, filename, filepath, contentType, mExtraHeaders, eResponse)))
{

}

HttpClient::HttpClient(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse)  :
    m_pImpl(std::shared_ptr<HttpClientImpl>(new HttpClientImpl(method, target, vData,  mExtraHeaders, eResponse)))
{

}

const clientResponse& HttpClient::Run(const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    return m_pImpl->Run(connectionTimeout, processTimeout);
}

void HttpClient::Run(std::function<void(const clientResponse&, unsigned int )> pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout, const std::chrono::milliseconds& delay)
{

    ThreadPool::Get().Submit([=, pImpl=m_pImpl]
                             {
                                pmlLog(pml::LOG_TRACE, "pml::restgoose") << "HttpClient::Run #" << nRunId;
                                std::this_thread::sleep_for(delay);
                                pmlLog(pml::LOG_TRACE, "pml::restgoose") << "HttpClient::RunAsync " << nRunId;
                                pImpl->RunAsync(pCallback, nRunId, connectionTimeout, processTimeout);
                            });
}

void HttpClient::SetUploadProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback)
{
    m_pImpl->SetUploadProgressCallback(pCallback);
}

void HttpClient::SetDownloadProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback)
{
    m_pImpl->SetDownloadProgressCallback(pCallback);
}

void HttpClient::Cancel()
{
    m_pImpl->Cancel();
}

bool HttpClient::SetBasicAuthentication(const userName& user, const password& pass)
{
    return m_pImpl->SetBasicAuthentication(user, pass);
}

bool HttpClient::SetBearerAuthentication(const std::string& sToken)
{
    return m_pImpl->SetBearerAuthentication(sToken);
}

bool HttpClient::SetCertificateAuthority(const fileLocation& ca)
{
    return m_pImpl->SetCertificateAuthority(ca);
}

bool HttpClient::SetClientCertificate(const fileLocation& cert, const fileLocation& key)
{
    return m_pImpl->SetClientCertificate(cert, key);
}

bool HttpClient::SetMethod(const httpMethod& method)
{
    return m_pImpl->SetMethod(method);
}

bool HttpClient::SetEndpoint(const endpoint& target)
{
    return m_pImpl->SetEndpoint(target);
}

bool HttpClient::SetData(const Json::Value& jsData)
{
    return m_pImpl->SetData(jsData);
}

bool HttpClient::SetData(const textData& data)
{
    return m_pImpl->SetData(data);
}

bool HttpClient::SetFile(const textData& filename, const fileLocation& filepath)
{
    return m_pImpl->SetFile(filename, filepath);
}

bool HttpClient::SetPartData(const std::vector<partData>& vData)
{
    return m_pImpl->SetPartData(vData);
}

bool HttpClient::AddHeaders(const std::map<headerName, headerValue>& mHeaders)
{
    return m_pImpl->AddHeaders(mHeaders);
}

bool HttpClient::SetExpectedResponse(const clientResponse::enumResponse eResponse)
{
    return m_pImpl->SetExpectedResponse(eResponse);
}


void HttpClient::UseProxy(const std::string& proxy)
{
    m_pImpl->UseProxy(endpoint(proxy));
}
