#include "httpclientimpl.h"

#include <chrono>
#include <iostream>
#include <thread>

#include "log.h"
#include "mongoose.h"
#include "threadpool.h"
#include "utils.h"


using namespace std::placeholders;

namespace pml::restgoose
{


const std::string kBoundary = "--------44E4975E-3D60";
const std::string kCrLf = "\r\n";
const std::string kBoundaryDivider = "--"+kBoundary+kCrLf;
const std::string kBoundaryLast = "--"+kBoundary+"--"+kCrLf;
const std::string kContentDisposition = "Content-Disposition: form-data; name=\"";
const std::string kCloseQuote = "\"";
const std::string kFilename = "; filename=\"";
const headerValue kMultipart = headerValue("multipart/form-data; boundary="+kBoundary);



HttpClientImpl::HttpClientImpl()=default;

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{

}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{
    m_vPostData.emplace_back(partName(""), data);
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const Json::Value& jsData, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(headerValue("application/json")),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{
    auto sData = ConvertFromJson(jsData);
    pml::log::trace("pml::restgoose") << "HttpClient: convert from json to '" << sData << "'";
    m_vPostData.emplace_back(partName(""), textData(sData));
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& filename, const std::filesystem::path& filepath, const headerValue& contentType, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{
    m_vPostData.emplace_back(partName(""), filename, filepath);
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue>& mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(kMultipart),
    m_vPostData(vData),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{

}

void HttpClientImpl::HandleConnectEvent(mg_connection* pConnection)
{
    m_eStatus = HttpClientImpl::kConnected;
    if(m_proxy.Get().empty())
    {
        HandleConnectEventDirect(pConnection);
    }
    else
    {
        HandleConnectEventToProxy(pConnection);
    }
}

void HttpClientImpl::HandleConnectEventDirect(mg_connection* pConnection)
{
    pml::log::trace("pml::restgoose") << "Direct Connection";

    //if https then do so
    std::string sProto("http://");
    mg_str host = mg_url_host(m_point.second.Get().c_str());
    if(mg_url_is_ssl(m_point.second.Get().c_str()))
    {
        sProto = "https://";

        pml::log::trace("pml::restgoose") << "HttpClient\tConnection is https";
        mg_tls_opts opts{};
        opts.name = host;

        if(m_ca.empty() == false)
        {
            opts.ca = mg_str(m_ca.c_str());
        }
        if(m_Cert.empty() == false && m_Key.empty() == false)
        {
            opts.cert = mg_str(m_Cert.c_str());
            opts.key = mg_str(m_Key.c_str());
        }
        mg_tls_init(pConnection, &opts);
    }

    //start with http:// then find next /
    size_t nStart = m_point.second.Get().substr(sProto.length()).find('/');
    std::string sEndpoint = m_point.second.Get().substr(nStart+sProto.length());

    //send the connection headers
    std::stringstream ss;
    ss << m_point.first.Get() << " " << sEndpoint << " HTTP/1.1" << kCrLf
       << "Host: " << std::string(host.buf, host.len) << kCrLf;
    if(m_contentType.Get().empty() == false)
    {
        ss << "Content-Type: " << m_contentType.Get() << kCrLf;
    }
    ss << "Content-Length: " << WorkoutDataSize() << kCrLf;
       //<< "Expect: 100-continue\r\n"
    for(const auto& [name, value] : m_mHeaders)
    {
        ss << name.Get() << ": " << value.Get() << kCrLf;
    }
    ss << kCrLf;
    auto str = ss.str();

    pml::log::trace("pml::restgoose") << "HttpClient:SendHeader: " << str;
    mg_send(pConnection, str.c_str(), str.length());
}

void HttpClientImpl::HandleConnectEventToProxy(mg_connection* pConnection)
{
    pml::log::trace("pml::restgoose") << "Connected to Proxy";
    //if https then do so
    std::string sProto("http://");
    auto host = mg_url_host(m_point.second.Get().c_str());
    //auto port = mg_url_port(m_point.second.Get().c_str());

    if(mg_url_is_ssl(m_point.second.Get().c_str()))
    {
        sProto = "https://";

        pml::log::trace("pml::restgoose") << "HttpClient\tConnection is https";
        mg_tls_opts opts{};
        opts.name = host;

        if(m_ca.empty() == false)
        {
            opts.ca = mg_str(m_ca.string().c_str());
        }
        if(m_Cert.empty() == false && m_Key.empty() == false)
        {
            opts.cert = mg_str(m_Cert.string().c_str());
            opts.key = mg_str(m_Key.string().c_str());
        }
        mg_tls_init(pConnection, &opts);
    }

    //if we've alreday got the http:// bit don't repeat it
    std::string sEndpoint;
    if(m_point.second.Get().substr(0, sProto.length()) == sProto)
    {
        sProto = "";
        sEndpoint = m_point.second.Get().substr(sProto.length());
    }

    m_bConnectedViaProxy = true;

    auto vSplit = SplitString(sEndpoint, '/');

    //send the connection headers
    std::stringstream ss;
    ss << m_point.first.Get() << " " << sProto << m_point.second.Get() << " HTTP/1.1" << kCrLf
       << "Host: " << vSplit[0] << kCrLf;

    if(auto nLength = WorkoutDataSize(); nLength != 0 && m_contentType.Get().empty() == false)
    {
        ss << "Content-Type: " << m_contentType.Get() << kCrLf;
        ss << "Content-Length: " << nLength << kCrLf;
    }
    ss << "Accept: */*" << kCrLf;

       //<< "Expect: 100-continue\r\n"
    for(const auto& [name, value] : m_mHeaders)
    {
        ss << name.Get() << ": " << value.Get() << kCrLf;
    }
    ss << kCrLf;
    auto str = ss.str();

    pml::log::trace("pml::restgoose") << "HttpClient:SendHeader: " << str;
    mg_send(pConnection, str.c_str(), str.length());
}

void HttpClientImpl::HandleReadEvent(mg_connection* pConnection)
{
    if(m_proxy.Get().empty() == false && m_bConnectedViaProxy == false)
    {
        mg_http_message hm;
        auto n = mg_http_parse((char*)pConnection->recv.buf, pConnection->recv.len, &hm);
        if(n > 0)
        {
            //auto host = mg_url_host(m_point.second.Get().c_str());
            m_bConnectedViaProxy = true;

            pml::log::trace("pml::restgoose") << "HttpClient:Connected Via Proxy: " << std::string(hm.uri.buf, hm.uri.len);

            mg_iobuf_del(&pConnection->recv, 0, n);

            auto vSplit = SplitString(m_point.second.Get(), '/');
            if(vSplit.size() < 2)
            {
                vSplit.emplace_back("/");
            }

            //send the connection headers
            std::stringstream ss;
            ss << m_point.first.Get() << " /" << vSplit[1] << " HTTP/1.1" << kCrLf
               << "Host: " << vSplit[0] << kCrLf;
            if(m_contentType.Get().empty() == false)
            {
                ss << "Content-Type: " << m_contentType.Get() << kCrLf;
            }
            ss << "Content-Length: " << WorkoutDataSize() << kCrLf;
               //<< "Expect: 100-continue\r\n"
            for(const auto& [name, value] : m_mHeaders)
            {
                ss << name.Get() << ": " << value.Get() << kCrLf;
            }
            ss << kCrLf;
            auto str = ss.str();

            pml::log::trace("pml::restgoose") << "HttpClient:SendHeader: " << str;
            mg_send(pConnection, str.c_str(), str.length());
        }

    }
}

void HttpClientImpl::GetContentHeaders(mg_http_message* pReply)
{
    auto nMax = sizeof(pReply->headers) / sizeof(pReply->headers[0]);
    for (size_t i = 0; i < nMax && pReply->headers[i].name.len > 0; i++)
    {
        m_response.mHeaders.try_emplace(headerName(std::string(pReply->headers[i].name.buf, pReply->headers[i].name.len)),
                                        std::string(pReply->headers[i].value.buf, pReply->headers[i].value.len));
    }

    auto itType = m_response.mHeaders.find(headerName("Content-Type"));
    auto itLen = m_response.mHeaders.find(headerName("Content-Length"));

    if(itType != m_response.mHeaders.end())
    {
        m_response.contentType = itType->second;

        if(auto nPos = m_response.contentType.Get().find(';'); nPos != std::string::npos)
        {
            m_response.contentType = headerValue(m_response.contentType.Get().substr(0, nPos));
        }

        pml::log::trace("pml::restgoose") << "HttpClient::Content-Type: " << m_response.contentType;


        //if not text or application/json or sdp then treat as a file for now
        //@todo we need to work out how to decide if binary or text...
        switch(m_eResponse)
        {
            case clientResponse::enumResponse::kFile:
                m_response.bBinary = true;
                break;
            case clientResponse::enumResponse::kText:
                m_response.bBinary = false;
                break;
            default:
                m_response.bBinary = (m_response.contentType.Get().find("text/") == std::string::npos &&
                              m_response.contentType.Get().find("application/json") == std::string::npos &&
                              m_response.contentType.Get().find("application/sdp") == std::string::npos &&
                              m_response.contentType.Get().find("application/xml") == std::string::npos);
        }

        if(m_response.bBinary)
        {   //if binary data then we save it to a file and pass back the filename
            m_response.data.Get() = CreateTmpFileName("/tmp").string();
            m_ofs.open(m_response.data.Get());
        }

    }
    if(itLen != m_response.mHeaders.end())
    {
        try
        {
            m_response.nContentLength = std::stoul(itLen->second.Get());

            pml::log::trace("pml::restgoose") << "HttpClient::Content-Length: " << m_response.nContentLength;

        }
        catch(const std::invalid_argument& e)
        {
            pml::log::trace("pml::restgoose") << "RestGoose:HttpClient\tFailed to get content length: " << e.what() << " " << itLen->second.Get();
        }
        catch(const std::out_of_range& e)
        {
            pml::log::trace("pml::restgoose") << "RestGoose:HttpClient\tFailed to get content length: " << e.what() << " " << itLen->second.Get();
        }
    }

}

void HttpClientImpl::GetResponseCode(mg_http_message* pReply)
{
    try
    {
        m_response.nHttpCode = static_cast<unsigned short>(std::stoul(std::string(pReply->uri.buf, pReply->uri.len)));
        pml::log::trace("pml::restgoose") << "HttpClient::Resonse code: " << m_response.nHttpCode;
    }
    catch(const std::invalid_argument& e)
    {
        m_response.nHttpCode = clientResponse::enumError::kErrorReply;
        m_eStatus = HttpClientImpl::kComplete;
    }
    catch(const std::out_of_range& e)
    {
        m_response.nHttpCode = clientResponse::enumError::kErrorReply;
        m_eStatus = HttpClientImpl::kComplete;
    }
}

void HttpClientImpl::HandleMessageEvent(mg_http_message* pReply)
{
    pml::log::trace("pml::restgoose") << "HttpClient:RawReply: " << std::string(pReply->message.buf, pReply->message.len);

    //Check the response code for relocation...
    GetResponseCode(pReply);
    GetContentHeaders(pReply);

    if(m_response.nHttpCode == 300 || m_response.nHttpCode == 301 || m_response.nHttpCode == 302)
    {   //redirects
        pml::log::trace("pml::restgoose") << "HttpClient:Reply: Redirect";

        if(auto var = mg_http_get_header(pReply, "Location"); var && var->len > 0)
        {
            m_response.data.Get().assign(var->buf, var->len);
        }
        m_eStatus = HttpClientImpl::kRedirecting;
    }
    else
    {
        m_response.nBytesReceived = m_response.nContentLength;

        if(m_response.bBinary == false)
        {
            m_response.data.Get().append(pReply->body.buf, pReply->body.len);

            pml::log::trace("pml::restgoose") << "HttpClient:Reply: " << m_response.data;
        }
        else if(m_ofs.is_open())
        {
            m_ofs.write(pReply->body.buf, pReply->body.len);
            m_ofs.close();
        }
        else
        {
            pml::log::trace("pml::restgoose") << "RestGoose:HttpClient\tSent binary data but could not open a file to write it to";
        }
        m_eStatus = HttpClientImpl::kComplete;
    }

}


void HttpClientImpl::HandleChunkEvent(mg_connection* pConnection, mg_http_message* pReply)
{
    /*
    pml::log::trace("pml::restgoose") << "HttpClient:RawChunk: " << std::string(pReply->chunk.ptr, pReply->chunk.len);

    auto bTerminate = false;

    if(pReply->chunk.len != 0)
    {
        if(m_eStatus == HttpClientImpl::CONNECTED || m_eStatus == HttpClientImpl::SENDING)
        {
            GetResponseCode(pReply);
            GetContentHeaders(pReply);

            m_eStatus = HttpClientImpl::RECEIVING;
        }
        m_response.nBytesReceived += pReply->chunk.len;

        if(m_response.bBinary)
        {
            if(m_ofs.is_open())
            {
                m_ofs.write(pReply->chunk.ptr, pReply->chunk.len);
            }
        }
        else
        {
            m_response.data.Get().append(pReply->chunk.ptr, pReply->chunk.len);
        }
    }
    else
    {
        bTerminate = true;
    }

    mg_http_delete_chunk(pConnection, pReply);

    if(m_pDownloadProgressCallback)
    {
        m_pDownloadProgressCallback(m_response.nBytesReceived, m_response.nContentLength);
    }

    if((m_response.nContentLength != 0 && m_response.nBytesReceived >= m_response.nContentLength) || bTerminate)
    {
        if(m_ofs.is_open())
        {
            m_ofs.close();
        }
        m_eStatus = HttpClientImpl::COMPLETE;
    }
    */
}


void HttpClientImpl::SetupRedirect()
{
    m_eStatus = HttpClientImpl::kConnecting;
    if(m_response.data.Get().find("://") != std::string::npos)
    {
        m_point.second = endpoint(m_response.data.Get());
    }
    else
    {
        m_point.second = endpoint(m_point.second.Get()+m_response.data.Get());
    }
}

void HttpClientImpl::HandleErrorEvent(const char* error)
{
    m_response.nHttpCode = clientResponse::enumError::kErrorConnection;
    m_response.data.Get() = error;
    m_eStatus = HttpClientImpl::kComplete;

    pml::log::trace("pml::restgoose") << "RestGoose:HttpClient\tError event: " << m_response.data.Get();
}

static void evt_handler(mg_connection* pConnection, int nEvent, void* pEventData)
{
    auto pMessage = reinterpret_cast<HttpClientImpl*>(pConnection->fn_data);

    if(nEvent == MG_EV_CONNECT)
    {
        pMessage->HandleConnectEvent(pConnection);
    }
    else if(nEvent == MG_EV_READ)
    {
        pMessage->HandleReadEvent(pConnection);
    }
    else if(nEvent == MG_EV_WRITE)
    {
        pMessage->HandleWroteEvent(pConnection, *(reinterpret_cast<int*>(pEventData)));
    }
    else if(nEvent == MG_EV_HTTP_MSG)
    {
        auto pReply = reinterpret_cast<mg_http_message*>(pEventData);
        pMessage->HandleMessageEvent(pReply);
        pConnection->is_closing = 1;
    }
    /*else if(nEvent == MG_EV_HTTP_CHUNK)
    {
        pml::log::trace("pml::restgoose") << "HttpClient:MG_EV_HTTP_CHUNK";
        auto pReply = reinterpret_cast<mg_http_message*>(pEventData);
        pMessage->HandleChunkEvent(pConnection, pReply);
    }*/
    else if(nEvent == MG_EV_ERROR)
    {
        pMessage->HandleErrorEvent(reinterpret_cast<const char*>(pEventData));
    }
}

void HttpClientImpl::RunAsync(const std::function<void(const clientResponse&, unsigned int, const std::string& )>& pCallback, unsigned int nRunId, const std::string& sUserData, const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    m_pAsyncCallback = pCallback;
    m_nRunId = nRunId;
    m_sUserData = sUserData;
    pml::log::trace("pml::restgoose") << "RestGoose:HttpClient::RunAsync: nRunId = " << nRunId << " Endpoint: " << m_point.second;
    Run(connectionTimeout, processTimeout);
}

void HttpClientImpl::RunAsyncOld(const std::function<void(const clientResponse&, unsigned int)>& pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    m_pAsyncCallbackV1 = pCallback;
    m_nRunId = nRunId;
    pml::log::trace("pml::restgoose") << "RestGoose:HttpClient::RunAsync: nRunId = " << nRunId << " Endpoint: " << m_point.second;
    Run(connectionTimeout, processTimeout);
}


const clientResponse& HttpClientImpl::Run(const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    mg_log_set(0);

    m_connectionTimeout = connectionTimeout;
    m_processTimeout = processTimeout;
    m_bConnectedViaProxy = false;

    pml::log::trace("pml::restgoose") << "RestGoose:HttpClient::Run - connect to " << m_point.second;
    mg_mgr mgr;
    mg_mgr_init(&mgr);

    auto theEndpoint =  m_proxy.Get().empty() ? m_point.second.Get().c_str() : m_proxy.Get().c_str();
    if(auto pConnection = mg_http_connect(&mgr, theEndpoint, evt_handler, (void*)this); pConnection == nullptr)
    {
        pml::log::error("pml::restgoose") << "RestGoose:HttpClient\tCould not create connection";
        m_response.nHttpCode = clientResponse::enumError::kErrorSetup;
    }
    else
    {
        if(m_proxy.Get().empty())
        {
            pml::log::trace("pml::restgoose") << "RestGoose:HttpClient::Run - connecting " << m_point.second;
        }
        else
        {
            pml::log::trace("pml::restgoose") << "RestGoose:HttpClient::Run - connecting " << m_point.second << " via proxy " << m_proxy;
        }
        DoLoop(mgr);
        if(m_eStatus == HttpClientImpl::kRedirecting)
        {
            SetupRedirect();
            return Run();
        }
        else if(m_eStatus == HttpClientImpl::kComplete)
        {
            pml::log::trace("pml::restgoose") << "RestGoose:HttpClient\tComplete";
        }
        else
        {
            pml::log::trace("pml::restgoose") << "RestGoose:HttpClient\tTimed out";
            //timed out
            m_response.nHttpCode = clientResponse::enumError::kErrorTime;
        }
    }
    if(m_pAsyncCallback)
    {
        m_pAsyncCallback(m_response, m_nRunId, m_sUserData);
    }
    else if(m_pAsyncCallbackV1)
    {
        m_pAsyncCallbackV1(m_response, m_nRunId);
    }
    mg_mgr_free(&mgr);
    return m_response;
}

void HttpClientImpl::DoLoop(mg_mgr& mgr) const
{
    auto start = std::chrono::system_clock::now();
    while(m_eStatus != HttpClientImpl::kRedirecting && m_eStatus != HttpClientImpl::kComplete)
    {
        mg_mgr_poll(&mgr, 500);

        auto now = std::chrono::system_clock::now();

        if((m_eStatus == kConnecting && std::chrono::duration_cast<std::chrono::milliseconds>(now-start) > m_connectionTimeout) ||
           (m_eStatus != kConnecting && m_processTimeout.count() != 0 && std::chrono::duration_cast<std::chrono::milliseconds>(now-start) > m_processTimeout))
        {
            pml::log::trace("pml::restgoose") << "HttpClientImpl\tTimeout";
            break;
        }
    }
}

unsigned long HttpClientImpl::WorkoutFileSize(const std::filesystem::path& filename)
{
    unsigned long nLength = 0;
    m_ifs.open(filename.string(), std::ifstream::ate | std::ifstream::binary);
    if(m_ifs.is_open() == false)
    {
        m_response.nHttpCode = clientResponse::enumError::kErrorFileRead;
        m_eStatus = kComplete;
    }
    else
    {
        nLength = m_ifs.tellg();
        m_ifs.close();
    }
    return nLength;
}

unsigned long HttpClientImpl::WorkoutDataSize()
{
    if(m_vPostData.empty())
    {
        m_nContentLength = 0;
    }
    else if(m_vPostData.size() == 1)
    {
        if(m_vPostData.back().filepath.string().empty())
        {
            m_nContentLength =  m_vPostData.back().data.Get().length();
        }
        else
        {
            m_nContentLength = WorkoutFileSize(m_vPostData.back().filepath);
        }
    }
    else
    {
        for(auto& part : m_vPostData)
        {
            if(part.filepath.string().empty())
            {
                part.sHeader = kBoundaryDivider;
                part.sHeader += kContentDisposition+part.name.Get()+kCloseQuote+kCrLf;
                part.sHeader += kCrLf;

                m_nContentLength += part.sHeader.length();
                m_nContentLength += part.data.Get().length();
                m_nContentLength += kCrLf.length();
            }
            else
            {
                part.sHeader = kBoundaryDivider;
                part.sHeader += kContentDisposition+part.name.Get()+kCloseQuote+kFilename+part.data.Get()+kCloseQuote+kCrLf;
                part.sHeader += "Content-Type: application/octet-stream\r\n";
                part.sHeader += "Content-Transfer-Encoding: binary\r\n";
                part.sHeader += kCrLf;

                m_nContentLength += part.sHeader.length();
                m_nContentLength += WorkoutFileSize(part.filepath);
                m_nContentLength += kCrLf.length();
            }
        }
        //last boundary
        m_nContentLength += kBoundaryLast.length();

    }
    return m_nContentLength;
}


void HttpClientImpl::HandleWroteEvent(mg_connection* pConnection, int nBytes)
{
    if(m_eStatus != kConnected)
    {
        m_nBytesSent += nBytes;
    }

    if(m_pUploadProgressCallback)
    {
        m_pUploadProgressCallback(m_nBytesSent, m_nContentLength);
    }


    if(m_vPostData.size() == 1)
    {
        HandleSimpleWroteEvent(pConnection);
    }
    else
    {
        HandleMultipartWroteEvent(pConnection);
    }


}

void HttpClientImpl::HandleSimpleWroteEvent(mg_connection* pConnection)
{
    if(m_vPostData.back().filepath.string().empty())    //no filepath so sending the text
    {
        if(m_eStatus == kConnected)
        {
            mg_send(pConnection, m_vPostData.back().data.Get().c_str(), m_vPostData.back().data.Get().length());
            m_eStatus = kSending;
        }
    }
    else
    {
        SendFile(pConnection, m_vPostData.back().filepath, (m_eStatus == kConnected));
        m_eStatus = kSending;
    }
}

bool HttpClientImpl::SendFile(mg_connection* pConnection, const std::filesystem::path& filepath, bool bOpen)
{
    if(bOpen)
    {
        m_ifs.open(filepath.string());
        if(m_ifs.is_open() == false)
        {
            pml::log::trace("pml::restgoose") << "HttpClient: Unable to open file " << filepath << " to upload.";
            m_response.nHttpCode = clientResponse::enumError::kErrorFileRead;
            m_eStatus = kComplete;
        }
    }
    if(m_ifs.is_open())
    {
        std::array<char, 61440> buffer;
        if(m_ifs.read(buffer.data(), 61440) || m_ifs.eof())
        {
            mg_send(pConnection, buffer.data(), m_ifs.gcount());

            if(m_ifs.eof()) //finished sending
            {
                m_ifs.close();
                return true;
            }
        }
        else if(m_ifs.bad())
        {
            pml::log::trace("pml::restgoose") << "HttpClient: Unable to read file " << filepath << " to upload.";
            m_response.nHttpCode = clientResponse::enumError::kErrorFileRead;
            m_eStatus = kComplete;
        }
    }
    return false;
}

void HttpClientImpl::SetUploadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback)
{
    m_pUploadProgressCallback = pCallback;
}

void HttpClientImpl::SetDownloadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback)
{
    m_pDownloadProgressCallback = pCallback;
}

void HttpClientImpl::HandleMultipartWroteEvent(mg_connection* pConnection)
{

    //if connected then deal with first part
    if(m_eStatus == kConnected)
    {
        m_nPostPart = 0;
        m_eStatus = kSending;
    }
    if(m_nPostPart < m_vPostData.size())
    {
        if(m_ifs.is_open() == false)    //not currently sending a file
        {
            mg_send(pConnection, m_vPostData[m_nPostPart].sHeader.c_str(), m_vPostData[m_nPostPart].sHeader.length());

            if(m_vPostData[m_nPostPart].filepath.string().empty())
            {
                mg_send(pConnection, m_vPostData[m_nPostPart].data.Get().c_str(), m_vPostData[m_nPostPart].data.Get().length());
                mg_send(pConnection, kCrLf.c_str(), kCrLf.length());

                ++m_nPostPart;
            }
            else if(SendFile(pConnection, m_vPostData[m_nPostPart].filepath, true) == true)
            {   //opened and sent in one fell swoop
                mg_send(pConnection, kCrLf.c_str(), kCrLf.length());
                ++m_nPostPart;
            }
        }
        else
        {

            if(SendFile(pConnection, m_vPostData[m_nPostPart].filepath, false) == true)
            {   //all sent
                mg_send(pConnection, kCrLf.c_str(), kCrLf.length());
                ++m_nPostPart;
            }
        }
        if(m_nPostPart == m_vPostData.size())
        {
            mg_send(pConnection, kBoundaryLast.c_str(), kBoundaryLast.length());
        }
    }
}

HttpClientImpl::~HttpClientImpl()=default;


void HttpClientImpl::Cancel()
{
    m_eStatus = kComplete;
    m_response.nHttpCode = clientResponse::enumError::kUserCancelled;
}


bool HttpClientImpl::SetBasicAuthentication(const userName& user, const password& pass)
{
    if(m_pAsyncCallback == nullptr)
    {
        std::string str = user.Get()+":"+pass.Get();
        char buff[128];
        mg_base64_encode((const unsigned char*)str.c_str(), str.length(), buff, 128);
        m_mHeaders[headerName("Authorization")] =  headerValue("Basic "+std::string(buff));
        return true;
    }
    return false;
}

bool HttpClientImpl::SetBearerAuthentication(const std::string& sToken)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_mHeaders[headerName("Authorization")] =  headerValue("Bearer "+sToken);
        return true;
    }
    return false;
}

bool HttpClientImpl::SetMethod(const httpMethod& method)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_point = methodpoint(method, m_point.second);
        return true;
    }
    return false;
}

bool HttpClientImpl::SetEndpoint(const endpoint& target)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_point = methodpoint(m_point.first, target);
        return true;
    }
    return false;
}

bool HttpClientImpl::SetData(const Json::Value& jsData)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_vPostData.clear();
        m_vPostData.emplace_back(partName(""), textData(ConvertFromJson(jsData)));
        return true;
    }
    return false;
}

bool HttpClientImpl::SetData(const textData& data)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_vPostData.clear();
        m_vPostData.emplace_back(partName(""), data);
        return true;
    }
    return false;
}

bool HttpClientImpl::SetFile(const textData& filename, const std::filesystem::path& filepath)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_vPostData.clear();
        m_vPostData.emplace_back(partName(""), filename, filepath);
        return true;
    }
    return false;
}

bool HttpClientImpl::SetPartData(const std::vector<partData>& vData)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_vPostData = vData;
        return true;
    }
    return false;
}

bool HttpClientImpl::AddHeaders(const std::map<headerName, headerValue>& mHeaders)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_mHeaders.insert(mHeaders.begin(), mHeaders.end());
        return true;
    }
    return false;
}

bool HttpClientImpl::SetExpectedResponse(const clientResponse::enumResponse eResponse)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_eResponse = eResponse;
        return true;
    }
    return false;
}

bool HttpClientImpl::SetCertificateAuthority(const std::filesystem::path& ca)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_ca = ca;
        return true;
    }
    return false;
}

bool HttpClientImpl::SetClientCertificate(const std::filesystem::path& cert, const std::filesystem::path& key)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_Cert = cert;
        m_Key = key;
        return true;
    }
    return false;
}

void HttpClientImpl::UseProxy(const endpoint& proxy)
{
    m_proxy = proxy;
}

}