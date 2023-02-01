#include "httpclientimpl.h"
#include <thread>
#include "log.h"
#include "mongoose.h"
#include <iostream>
#include <chrono>
#include "utils.h"
#include "threadpool.h"


using namespace pml::restgoose;
using namespace std::placeholders;

static const std::string BOUNDARY = "--------44E4975E-3D60";
static const std::string CRLF = "\r\n";
static const std::string BOUNDARY_DIVIDER = "--"+BOUNDARY+CRLF;
static const std::string BOUNDARY_LAST = "--"+BOUNDARY+"--"+CRLF;
static const std::string CONTENT_DISPOSITION = "Content-Disposition: form-data; name=\"";
static const std::string CLOSE_QUOTE = "\"";
static const std::string FILENAME = "; filename=\"";
static const headerValue MULTIPART = headerValue("multipart/form-data; boundary="+BOUNDARY);



HttpClientImpl::HttpClientImpl()
{

}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{

}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{
    m_vPostData.push_back(partData(partName(""), data));
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const Json::Value& jsData, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(headerValue("application/json")),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{
    auto sData = ConvertFromJson(jsData);
    pmlLog(pml::LOG_TRACE) << "HttpClient: convert from json to '" << sData << "'";
    m_vPostData.push_back(partData(partName(""), textData(sData)));
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& filename, const fileLocation& filepath, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{
    m_vPostData.push_back(partData(partName(""), filename, filepath));
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue> mExtraHeaders, clientResponse::enumResponse eResponse) :
    m_point(method, target),
    m_contentType(MULTIPART),//"text/plain"),
    m_vPostData(vData),
    m_mHeaders(mExtraHeaders),
    m_eResponse(eResponse)
{

}


void HttpClientImpl::HandleConnectEvent(mg_connection* pConnection)
{

    m_eStatus = HttpClientImpl::CONNECTED;

    //if https then do so
    std::string sProto("http://");
    mg_str host = mg_url_host(m_point.second.Get().c_str());
    if(mg_url_is_ssl(m_point.second.Get().c_str()))
    {
        sProto = "https://";

        pmlLog(pml::LOG_TRACE) << "HttpClient\tConnection is https";
        mg_tls_opts opts{};
        opts.srvname = host;

        if(m_ca.Get().empty() == false)
        {
            opts.ca = m_ca.Get().c_str();
        }
        if(m_Cert.Get().empty() == false && m_Key.Get().empty() == false)
        {
            opts.cert = m_Cert.Get().c_str();
            opts.certkey = m_Key.Get().c_str();
        }
        mg_tls_init(pConnection, &opts);
    }

    //start with http:// then find next /
    size_t nStart = m_point.second.Get().substr(sProto.length()).find('/');
    std::string sEndpoint = m_point.second.Get().substr(nStart+sProto.length());

    //send the connection headers
    std::stringstream ss;
    ss << m_point.first.Get() << " " << sEndpoint << " HTTP/1.1" << CRLF
       << "Host: " << std::string(host.ptr, host.len) << CRLF;
    if(m_contentType.Get().empty() == false)
    {
        ss << "Content-Type: " << m_contentType.Get() << CRLF;
    }
    ss << "Content-Length: " << WorkoutDataSize() << CRLF;
       //<< "Expect: 100-continue\r\n"
    for(auto pairHeader : m_mHeaders)
    {
        ss << pairHeader.first.Get() << ": " << pairHeader.second.Get() << CRLF;
    }
    ss << CRLF;
    auto str = ss.str();

    pmlLog(pml::LOG_TRACE) << "HttpClient:SendHeader: " << str;
    mg_send(pConnection, str.c_str(), str.length());

}


void HttpClientImpl::GetContentHeaders(mg_http_message* pReply)
{
    auto nMax = sizeof(pReply->headers) / sizeof(pReply->headers[0]);
    for (size_t i = 0; i < nMax && pReply->headers[i].name.len > 0; i++)
    {
        m_response.mHeaders.insert({headerName(std::string(pReply->headers[i].name.ptr, pReply->headers[i].name.len)),
                                    headerValue(std::string(pReply->headers[i].value.ptr, pReply->headers[i].value.len))});
    }

    auto itType = m_response.mHeaders.find(headerName("Content-Type"));
    auto itLen = m_response.mHeaders.find(headerName("Content-Length"));

    if(itType != m_response.mHeaders.end())
    {
        m_response.contentType = itType->second;

        auto nPos = m_response.contentType.Get().find(';');
        if(nPos != std::string::npos)
        {
            m_response.contentType = headerValue(m_response.contentType.Get().substr(0, nPos));
        }

        pmlLog(pml::LOG_TRACE) << "HttpClient::Content-Type: " << m_response.contentType;


        //if not text or application/json or sdp then treat as a file for now
        //@todo we need to work out how to decide if binary or text...
        switch(m_eResponse)
        {
            case clientResponse::enumResponse::FILE:
                m_response.bBinary = true;
                break;
            case clientResponse::enumResponse::TEXT:
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
            m_response.data.Get() = CreateTmpFileName("/tmp/").Get();
            m_ofs.open(m_response.data.Get());
        }

    }
    if(itLen != m_response.mHeaders.end())
    {
        try
        {
            m_response.nContentLength = std::stoul(itLen->second.Get());

            pmlLog(pml::LOG_TRACE) << "HttpClient::Content-Length: " << m_response.nContentLength;

        }
        catch(const std::exception& e)
        {
            pmlLog(pml::LOG_WARN) << "RestGoose:HttpClient\tFailed to get content length: " << e.what() << " " << itLen->second.Get();
        }
    }

}

void HttpClientImpl::GetResponseCode(mg_http_message* pReply)
{
    try
    {
        m_response.nHttpCode = std::stoul(std::string(pReply->uri.ptr, pReply->uri.len));
        pmlLog(pml::LOG_TRACE) << "HttpClient::Resonse code: " << m_response.nHttpCode;
    }
    catch(const std::exception& e)
    {
        m_response.nHttpCode = clientResponse::enumError::ERROR_REPLY;
        m_eStatus = HttpClientImpl::COMPLETE;
    }
}

void HttpClientImpl::HandleMessageEvent(mg_http_message* pReply)
{
    pmlLog(pml::LOG_TRACE) << "HttpClient:RawReply: " << std::string(pReply->message.ptr, pReply->message.len);

    //Check the response code for relocation...
    GetResponseCode(pReply);
    GetContentHeaders(pReply);

    if(m_response.nHttpCode == 300 || m_response.nHttpCode == 301 || m_response.nHttpCode == 302)
    {   //redirects
        pmlLog(pml::LOG_TRACE) << "HttpClient:Reply: Redirect";

        auto var = mg_http_get_header(pReply, "Location");
        if(var && var->len > 0)
        {
            m_response.data.Get().assign(var->ptr, var->len);
        }
        m_eStatus = HttpClientImpl::REDIRECTING;
    }
    else
    {
        m_response.nBytesReceived = m_response.nContentLength;

        if(m_response.bBinary == false)
        {
            m_response.data.Get().append(pReply->body.ptr, pReply->body.len);

            pmlLog(pml::LOG_TRACE) << "HttpClient:Reply: " << m_response.data;
        }
        else if(m_ofs.is_open())
        {
            m_ofs.write(pReply->body.ptr, pReply->body.len);
            m_ofs.close();
        }
        else
        {
            pmlLog(pml::LOG_WARN) << "RestGoose:HttpClient\tSent binary data but could not open a file to write it to";
        }
        m_eStatus = HttpClientImpl::COMPLETE;
    }

}


void HttpClientImpl::HandleChunkEvent(mg_connection* pConnection, mg_http_message* pReply)
{
    pmlLog(pml::LOG_TRACE) << "HttpClient:RawChunk: " << std::string(pReply->chunk.ptr, pReply->chunk.len);

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
}


void HttpClientImpl::SetupRedirect()
{
    m_eStatus = HttpClientImpl::CONNECTING;
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
    m_response.nHttpCode = clientResponse::enumError::ERROR_CONNECTION;
    m_response.data.Get() = error;
    m_eStatus = HttpClientImpl::COMPLETE;

    pmlLog(pml::LOG_TRACE) << "RestGoose:HttpClient\tError event: " << m_response.data.Get();
}

static void evt_handler(mg_connection* pConnection, int nEvent, void* pEventData, void* pfnData)
{
    HttpClientImpl* pMessage = reinterpret_cast<HttpClientImpl*>(pfnData);

    if(nEvent == MG_EV_CONNECT)
    {
        pMessage->HandleConnectEvent(pConnection);
    }
    else if(nEvent == MG_EV_WRITE)
    {
        pMessage->HandleWroteEvent(pConnection, *(reinterpret_cast<int*>(pEventData)));
    }
    else if(nEvent == MG_EV_HTTP_MSG)
    {
        mg_http_message* pReply = reinterpret_cast<mg_http_message*>(pEventData);
        pMessage->HandleMessageEvent(pReply);
        pConnection->is_closing = 1;
    }
    else if(nEvent == MG_EV_HTTP_CHUNK)
    {
        pmlLog(pml::LOG_TRACE) << "HttpClient:MG_EV_HTTP_CHUNK";
        mg_http_message* pReply = reinterpret_cast<mg_http_message*>(pEventData);
        pMessage->HandleChunkEvent(pConnection, pReply);
    }
    else if(nEvent == MG_EV_ERROR)
    {
        pMessage->HandleErrorEvent(reinterpret_cast<const char*>(pEventData));
    }
}

void HttpClientImpl::RunAsync(std::function<void(const clientResponse&, unsigned int)> pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    m_pAsyncCallback = pCallback;
    m_nRunId = nRunId;
    pmlLog(pml::LOG_TRACE) << "RestGoose:HttpClient::RunAsync: nRunId = " << nRunId << " Endpoint: " << m_point.second;// << " data: " << m_vPostData.back().filepath;
    Run(connectionTimeout, processTimeout);
}


const clientResponse& HttpClientImpl::Run(const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    mg_log_set(0);

    m_connectionTimeout = connectionTimeout;
    m_processTimeout = processTimeout;

    pmlLog(pml::LOG_TRACE) << "RestGoose:HttpClient::Run - connect to " << m_point.second;
    mg_mgr mgr;
    mg_mgr_init(&mgr);
    auto pConnection = mg_http_connect(&mgr, m_point.second.Get().c_str(), evt_handler, (void*)this);
    if(pConnection == nullptr)
    {
        pmlLog(pml::LOG_ERROR) << "RestGoose:HttpClient\tCould not create connection";
        m_response.nHttpCode = clientResponse::enumError::ERROR_SETUP;
    }
    else
    {
        pmlLog(pml::LOG_TRACE) << "RestGoose:HttpClient::Run - connected to " << m_point.second;
        DoLoop(mgr);
        if(m_eStatus == HttpClientImpl::REDIRECTING)
        {
            SetupRedirect();
            return Run();
        }
        else if(m_eStatus == HttpClientImpl::COMPLETE)
        {
            pmlLog(pml::LOG_TRACE) << "RestGoose:HttpClient\tComplete";
        }
        else
        {
            pmlLog(pml::LOG_TRACE) << "RestGoose:HttpClient\tTimed out";
            //timed out
            m_response.nHttpCode = clientResponse::enumError::ERROR_TIME;
        }
    }
    if(m_pAsyncCallback)
    {
        m_pAsyncCallback(m_response, m_nRunId);
    }
    mg_mgr_free(&mgr);
    return m_response;
}

void HttpClientImpl::DoLoop(mg_mgr& mgr)
{
    auto start = std::chrono::system_clock::now();
    while(m_eStatus != HttpClientImpl::REDIRECTING && m_eStatus != HttpClientImpl::COMPLETE)
    {
        mg_mgr_poll(&mgr, 500);

        auto now = std::chrono::system_clock::now();

        if((m_eStatus == CONNECTING && std::chrono::duration_cast<std::chrono::milliseconds>(now-start) > m_connectionTimeout) ||
           (m_eStatus != CONNECTING && m_processTimeout.count() != 0 && std::chrono::duration_cast<std::chrono::milliseconds>(now-start) > m_processTimeout))
        {
            pmlLog(pml::LOG_DEBUG) << "HttpClientImpl\tTimeout";
            break;
        }
    }
}

unsigned long HttpClientImpl::WorkoutFileSize(const fileLocation& filename)
{
    unsigned long nLength = 0;
    m_ifs.open(filename.Get(), std::ifstream::ate | std::ifstream::binary);
    if(m_ifs.is_open() == false)
    {
        m_response.nHttpCode = clientResponse::enumError::ERROR_FILE_READ;
        m_eStatus = COMPLETE;
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
        if(m_vPostData.back().filepath.Get().empty())
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
            if(part.filepath.Get().empty())
            {
                part.sHeader = BOUNDARY_DIVIDER;
                part.sHeader += CONTENT_DISPOSITION+part.name.Get()+CLOSE_QUOTE+CRLF;
                part.sHeader += CRLF;

                m_nContentLength += part.sHeader.length();
                m_nContentLength += part.data.Get().length();
                m_nContentLength += CRLF.length();
            }
            else
            {
                part.sHeader = BOUNDARY_DIVIDER;
                part.sHeader += CONTENT_DISPOSITION+part.name.Get()+CLOSE_QUOTE+FILENAME+part.data.Get()+CLOSE_QUOTE+CRLF;
                part.sHeader += "Content-Type: application/octet-stream\r\n";
                part.sHeader += "Content-Transfer-Encoding: binary\r\n";
                part.sHeader += CRLF;

                m_nContentLength += part.sHeader.length();
                m_nContentLength += WorkoutFileSize(part.filepath);
                m_nContentLength += CRLF.length();
            }
        }
        //last boundary
        m_nContentLength += BOUNDARY_LAST.length();

    }
    return m_nContentLength;
}


void HttpClientImpl::HandleWroteEvent(mg_connection* pConnection, int nBytes)
{
    if(m_eStatus != CONNECTED)
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
    if(m_vPostData.back().filepath.Get().empty())    //no filepath so sending the text
    {
        if(m_eStatus == CONNECTED)
        {
            mg_send(pConnection, m_vPostData.back().data.Get().c_str(), m_vPostData.back().data.Get().length());
            m_eStatus = SENDING;
        }
    }
    else
    {
        SendFile(pConnection, m_vPostData.back().filepath, (m_eStatus == CONNECTED));
        m_eStatus = SENDING;
    }
}

bool HttpClientImpl::SendFile(mg_connection* pConnection, const fileLocation& filepath, bool bOpen)
{
    if(bOpen)
    {
        m_ifs.open(filepath.Get());
        if(m_ifs.is_open() == false)
        {
            pmlLog(pml::LOG_ERROR) << "HttpClient: Unable to open file " << filepath << " to upload.";
            m_response.nHttpCode = clientResponse::enumError::ERROR_FILE_READ;
            m_eStatus = COMPLETE;
        }
    }
    if(m_ifs.is_open())
    {
        char buffer[61440];
        if(m_ifs.read(buffer, 61440) || m_ifs.eof())
        {
            mg_send(pConnection, buffer, m_ifs.gcount());

            if(m_ifs.eof()) //finished sending
            {
                m_ifs.close();
                return true;
            }
        }
        else if(m_ifs.bad())
        {
            pmlLog(pml::LOG_ERROR) << "HttpClient: Unable to read file " << filepath << " to upload.";
            m_response.nHttpCode = clientResponse::enumError::ERROR_FILE_READ;
            m_eStatus = COMPLETE;
        }
    }
    return false;
}

void HttpClientImpl::SetUploadProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback)
{
    m_pUploadProgressCallback = pCallback;
}

void HttpClientImpl::SetDownloadProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback)
{
    m_pDownloadProgressCallback = pCallback;
}

void HttpClientImpl::HandleMultipartWroteEvent(mg_connection* pConnection)
{

    //if connected then deal with first part
    if(m_eStatus == CONNECTED)
    {
        m_nPostPart = 0;
        m_eStatus = SENDING;
    }
    if(m_nPostPart < m_vPostData.size())
    {
        if(m_ifs.is_open() == false)    //not currently sending a file
        {
            mg_send(pConnection, m_vPostData[m_nPostPart].sHeader.c_str(), m_vPostData[m_nPostPart].sHeader.length());

            if(m_vPostData[m_nPostPart].filepath.Get().empty())
            {
                mg_send(pConnection, m_vPostData[m_nPostPart].data.Get().c_str(), m_vPostData[m_nPostPart].data.Get().length());
                mg_send(pConnection, CRLF.c_str(), CRLF.length());

                ++m_nPostPart;
            }
            else
            {
                if(SendFile(pConnection, m_vPostData[m_nPostPart].filepath, true) == true)
                {   //opened and sent in one fell swoop
                    mg_send(pConnection, CRLF.c_str(), CRLF.length());
                    ++m_nPostPart;
                }
            }
        }
        else
        {

            if(SendFile(pConnection, m_vPostData[m_nPostPart].filepath, false) == true)
            {   //all sent
                mg_send(pConnection, CRLF.c_str(), CRLF.length());
                ++m_nPostPart;
            }
        }
        if(m_nPostPart == m_vPostData.size())
        {
            mg_send(pConnection, BOUNDARY_LAST.c_str(), BOUNDARY_LAST.length());
        }
    }
}

HttpClientImpl::~HttpClientImpl()
{
}


void HttpClientImpl::Cancel()
{
    m_eStatus = COMPLETE;
    m_response.nHttpCode = clientResponse::enumError::USER_CANCELLED;
}


bool HttpClientImpl::SetBasicAuthentication(const userName& user, const password& pass)
{
    if(m_pAsyncCallback == nullptr)
    {
        std::string str = user.Get()+":"+pass.Get();
        char buff[128];
        mg_base64_encode((const unsigned char*)str.c_str(), str.length(), buff);
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
        m_vPostData.push_back(partData(partName(""), textData(ConvertFromJson(jsData))));
        return true;
    }
    return false;
}

bool HttpClientImpl::SetData(const textData& data)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_vPostData.clear();
        m_vPostData.push_back(partData(partName(""), data));
        return true;
    }
    return false;
}

bool HttpClientImpl::SetFile(const textData& filename, const fileLocation& filepath)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_vPostData.clear();
        m_vPostData.push_back(partData(partName(""), filename, filepath));
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

bool HttpClientImpl::SetCertificateAuthority(const fileLocation& ca)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_ca = ca;
        return true;
    }
    return false;
}

bool HttpClientImpl::SetClientCertificate(const fileLocation& cert, const fileLocation& key)
{
    if(m_pAsyncCallback == nullptr)
    {
        m_Cert = cert;
        m_Key = key;
        return true;
    }
    return false;
}
