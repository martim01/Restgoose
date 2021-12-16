#include "restfulclient.h"
#include <thread>
#include "log.h"
#include "mongoose.h"
#include <iostream>
#include <chrono>
#include "utils.h"

using namespace pml::restgoose;

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

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue> mExtraHeaders) :
    m_point(method, target),
    m_mHeaders(mExtraHeaders)
{

}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders) :
    m_point(method, target),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders)
{
    m_vPostData.push_back(partData(partName(""), data));
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& filename, const fileLocation& filepath, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders) :
    m_point(method, target),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders)
{
    m_vPostData.push_back(partData(partName(""), filename, filepath));
}

HttpClientImpl::HttpClientImpl(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue> mExtraHeaders) :
    m_point(method, target),
    m_contentType(MULTIPART),//"text/plain"),
    m_vPostData(vData),
    m_mHeaders(mExtraHeaders)
{

}


void HttpClientImpl::HandleConnectEvent(mg_connection* pConnection)
{
    pmlLog(pml::LOG_TRACE) << "HttpClientImpl\tConnect event";

    m_eStatus = HttpClientImpl::CONNECTED;

    //if https then do so
    mg_str host = mg_url_host(m_point.second.Get().c_str());
    if(mg_url_is_ssl(m_point.second.Get().c_str()))
    {
        mg_tls_opts opts = { .srvname = host};
        mg_tls_init(pConnection, &opts);
    }


    //send the connection headers
    std::stringstream ss;
    ss << m_point.first.Get() << " " << m_point.second.Get() << " HTTP/1.1" << CRLF
       << "Host: " << std::string(host.ptr, host.len) << CRLF
       << "Content-Type: " << m_contentType.Get() << CRLF
       << "Content-Length: " << WorkoutDataSize() << CRLF;
       //<< "Expect: 100-continue\r\n"
    for(auto pairHeader : m_mHeaders)
    {
        ss << pairHeader.first.Get() << ": " << pairHeader.second.Get() << CRLF;
    }
    ss << CRLF;
    auto str = ss.str();

    pmlLog() << "HttpClientImpl\t" << str;
    mg_send(pConnection, str.c_str(), str.length());

}


void HttpClientImpl::GetContentHeaders(mg_http_message* pReply)
{
    auto type = mg_http_get_header(pReply, "Content-Type");
    auto len = mg_http_get_header(pReply, "Content-Length");

    if(type)
    {
        m_response.contentType = headerValue(std::string(type->ptr, type->len));
        auto nPos = m_response.contentType.Get().find(';');
        if(nPos != std::string::npos)
        {
            m_response.contentType = headerValue(m_response.contentType.Get().substr(0, nPos));
        }
        //if not text or application/json or sdp then treat as a file for now
        m_response.bBinary = (m_response.contentType.Get().find("text/") == std::string::npos && m_response.contentType.Get().find("application/json") == std::string::npos &&
                              m_response.contentType.Get().find("application/sdp") == std::string::npos);

        if(m_response.bBinary)
        {   //if binary data then we save it to a file and pass back the filename
            m_response.sData = CreateTmpFileName("/tmp/").Get();
            m_ofs.open(m_response.sData);
        }

        pmlLog(pml::LOG_TRACE) << "HttpClientImpl\tContent-Type: " << m_response.contentType;
    }
    if(len)
    {
        try
        {
            m_response.nContentLength = std::stoul(std::string(len->ptr, len->len));
            pmlLog(pml::LOG_TRACE) << "HttpClientImpl\tContent-Length: " << m_response.nContentLength;
        }
        catch(const std::exception& e)
        {
            pmlLog(pml::LOG_WARN) << "HttpClientImpl\t" << e.what();
        }
    }
}

void HttpClientImpl::GetResponseCode(mg_http_message* pReply)
{
    try
    {
        m_response.nCode = std::stoul(std::string(pReply->uri.ptr, pReply->uri.len));
    }
    catch(const std::exception& e)
    {
        m_response.nCode = ERROR_REPLY;
        m_eStatus = HttpClientImpl::COMPLETE;
    }
}

void HttpClientImpl::HandleMessageEvent(mg_http_message* pReply)
{
    pmlLog(pml::LOG_TRACE) << "HttpClientImpl\tMessage event";

    //Check the response code for relocation...
    GetResponseCode(pReply);
    GetContentHeaders(pReply);

    if(m_response.nCode == 300 || m_response.nCode == 301 || m_response.nCode == 302)
    {   //redirects
        pmlLog(pml::LOG_TRACE) << "HttpClientImpl\tRedirecting";
        auto var = mg_http_get_header(pReply, "Location");
        if(var && var->len > 0)
        {
            m_response.sData.assign(var->ptr, var->len);
        }
        m_eStatus = HttpClientImpl::REDIRECTING;
    }
    else
    {
        m_response.nBytesReceived = m_response.nContentLength;

        if(m_response.bBinary == false)
        {
            m_response.sData.assign(pReply->body.ptr, pReply->body.len);
        }
        else if(m_ofs.is_open())
        {
            m_ofs.write(pReply->body.ptr, pReply->body.len);
            m_ofs.close();
        }
        else
        {
            pmlLog(pml::LOG_WARN) << "HttpClientImpl\tSent binary data but could not open a file to write it to";
        }
        m_eStatus = HttpClientImpl::COMPLETE;
    }

}


void HttpClientImpl::HandleChunkEvent(mg_connection* pConnection, mg_http_message* pReply)
{
    if(m_eStatus == HttpClientImpl::CONNECTED || m_eStatus == HttpClientImpl::SENDING)
    {
        pmlLog(pml::LOG_TRACE) << "HttpClientImpl\tFirst Chunk event";
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
        m_response.sData.append(pReply->chunk.ptr, pReply->chunk.len);
    }
    mg_http_delete_chunk(pConnection, pReply);

    if(m_response.nBytesReceived >= m_response.nContentLength)
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
    if(m_response.sData.find("://") != std::string::npos)
    {
        m_point.second = endpoint(m_response.sData);
    }
    else
    {
        m_point.second = endpoint(m_point.second.Get()+m_response.sData);
    }
}

void HttpClientImpl::HandleErrorEvent(const char* error)
{
    m_response.nCode = ERROR_CONNECTION;
    m_response.sData = error;
    m_eStatus = HttpClientImpl::COMPLETE;

    pmlLog(pml::LOG_TRACE) << "HttpClientImpl\tError event: " << m_response.sData;
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
    }
    else if(nEvent == MG_EV_HTTP_CHUNK)
    {
        mg_http_message* pReply = reinterpret_cast<mg_http_message*>(pEventData);
        pMessage->HandleChunkEvent(pConnection, pReply);
    }
    else if(nEvent == MG_EV_ERROR)
    {
        pMessage->HandleErrorEvent(reinterpret_cast<const char*>(pEventData));
    }
}



const clientResponse& HttpClientImpl::Run(const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
{
    mg_log_set("0");

    m_connectionTimeout = connectionTimeout;
    m_processTimeout = processTimeout;


    mg_mgr mgr;
    mg_mgr_init(&mgr);
    auto pConnection = mg_http_connect(&mgr, m_point.second.Get().c_str(), evt_handler, (void*)this);
    if(pConnection == nullptr)
    {
        pmlLog() << "Could not create connection";
        m_response.nCode = ERROR_SETUP;
    }
    else
    {
        DoLoop(mgr);

        if(m_eStatus == HttpClientImpl::REDIRECTING)
        {
            SetupRedirect();
            return Run();
        }
        else if(m_eStatus == HttpClientImpl::COMPLETE)
        {
            pmlLog() << "Complete";
        }
        else
        {
            pmlLog() << "Timed out";
            //timed out
            m_response.nCode = ERROR_TIMEOUT;
        }
    }
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
        m_response.nCode = ERROR_FILE_READ;
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

    if(m_pProgressCallback)
    {
        m_pProgressCallback(m_nBytesSent, m_nContentLength);
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
            m_response.nCode = ERROR_FILE_READ;
            m_eStatus = COMPLETE;
        }
    }
    if(m_ifs.is_open())
    {
        char buffer[61440];
        m_ifs.read(buffer, 61440);
        mg_send(pConnection, buffer, m_ifs.gcount());

        if(m_ifs.eof()) //finished sending
        {
            m_ifs.close();
            return true;
        }
    }
    return false;
}

void HttpClientImpl::SetProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback)
{
    m_pProgressCallback = pCallback;
}

void HttpClientImpl::HandleMultipartWroteEvent(mg_connection* pConnection)
{
    pmlLog() << "HandleMultipartWroteEvent\t" << m_nPostPart << ":" << m_vPostData.size();

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
            pmlLog() << "HandleMultipartWroteEvent\tSendHeader\t" << m_vPostData[m_nPostPart].sHeader;

            mg_send(pConnection, m_vPostData[m_nPostPart].sHeader.c_str(), m_vPostData[m_nPostPart].sHeader.length());

            if(m_vPostData[m_nPostPart].filepath.Get().empty())
            {
                pmlLog() << "HandleMultipartWroteEvent\tSendData\t" << m_vPostData[m_nPostPart].data;

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
            pmlLog() << "HandleMultipartWroteEvent\tSendLast\t" << BOUNDARY_LAST;
            mg_send(pConnection, BOUNDARY_LAST.c_str(), BOUNDARY_LAST.length());
        }
    }
}

HttpClientImpl::~HttpClientImpl() = default;
