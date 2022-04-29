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

    ThreadPool::Get().Submit([=, pImpl=m_pImpl]{
                             std::this_thread::sleep_for(delay);
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

void HttpClient::SetBasicAuthentication(const userName& user, const password& pass)
{
    m_pImpl->SetBasicAuthentication(user, pass);
}
