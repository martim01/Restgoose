#include "restfulclient.h"
#include <thread>
#include "log.h"
#include "mongoose.h"
#include <iostream>
#include <chrono>
#include "utils.h"


clientMessage::clientMessage() :
    m_eStatus(clientMessage::CONNECTING),
    m_connectionTimeout(5000),
    m_processTimeout(10000)
{

}

clientMessage::clientMessage(const methodpoint& point) :
    m_point(point),
    m_connectionTimeout(5000),
    m_processTimeout(10000)

{

}

clientMessage::clientMessage(const methodpoint& point, const textData& data, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders) :
    m_point(point),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders),
    m_connectionTimeout(5000),
    m_processTimeout(10000)
{
    partData part;
    part.sData = data.Get();
    m_vPostData.push_back(part);
}

clientMessage::clientMessage(const methodpoint& point, const fileName& name, const headerValue& contentType, const std::map<headerName, headerValue> mExtraHeaders) :
    m_point(point),
    m_contentType(contentType),
    m_mHeaders(mExtraHeaders),
    m_connectionTimeout(5000),
    m_processTimeout(10000)
{
    partData part;
    part.sFilename = name.Get();
    m_vPostData.push_back(part);
}

clientMessage::clientMessage(const methodpoint& point, const headerValue& contentType, const postData& vData, const std::map<headerName, headerValue> mExtraHeaders) :
    m_point(point),
    m_contentType(contentType),
    m_vPostData(vData),
    m_mHeaders(mExtraHeaders),
    m_connectionTimeout(5000),
    m_processTimeout(10000)
{

}


void clientMessage::HandleConnectEvent(mg_connection* pConnection)
{
    pmlLog(pml::LOG_TRACE) << "clientMessage\tConnect event";

    m_eStatus = clientMessage::CONNECTED;

    //if https then do so
    mg_str host = mg_url_host(m_point.second.Get().c_str());
    if(mg_url_is_ssl(m_point.second.Get().c_str()))
    {
        mg_tls_opts opts = { .srvname = host};
        mg_tls_init(pConnection, &opts);
    }


    //send the connection headers
    std::stringstream ss;
    ss << m_point.first.Get() << " " << m_point.second.Get() << " HTTP/1.1\r\n"
       << "Host: " << std::string(host.ptr, host.len) << "\r\n"
       << "Content-Type: " << m_contentType.Get() << "\r\n"
       << "Content-Length: " << WorkoutDataSize() << "\r\n";
       //<< "Expect: 100-continue\r\n"
    for(auto pairHeader : m_mHeaders)
    {
        ss << pairHeader.first.Get() << ": " << pairHeader.second.Get() << "\r\n";
    }
    ss << "\r\n";
    auto str = ss.str();
    mg_send(pConnection, str.c_str(), str.length());

    //pmlLog(pml::LOG_TRACE) << "clientMessage\tHeaders\t" << str;
}


void clientMessage::GetContentHeaders(mg_http_message* pReply)
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
            m_response.sData = CreateTmpFileName();
            m_ofs.open("/tmp/"+m_response.sData);
        }

        pmlLog(pml::LOG_TRACE) << "clientMessage\tContent-Type: " << m_response.contentType;
    }
    if(len)
    {
        try
        {
            m_response.nContentLength = std::stoul(std::string(len->ptr, len->len));
            pmlLog(pml::LOG_TRACE) << "clientMessage\tContent-Length: " << m_response.nContentLength;
        }
        catch(const std::exception& e)
        {
            pmlLog(pml::LOG_WARN) << "clientMessage\t" << e.what();
        }
    }
}

void clientMessage::GetResponseCode(mg_http_message* pReply)
{
    try
    {
        m_response.nCode = std::stoul(std::string(pReply->uri.ptr, pReply->uri.len));
    }
    catch(const std::exception& e)
    {
        m_response.nCode = ERROR_REPLY;
        m_eStatus = clientMessage::COMPLETE;
    }
}

void clientMessage::HandleMessageEvent(mg_http_message* pReply)
{
    pmlLog(pml::LOG_TRACE) << "clientMessage\tMessage event";

    //Check the response code for relocation...
    GetResponseCode(pReply);
    GetContentHeaders(pReply);

    if(m_response.nCode == 300 || m_response.nCode == 301 || m_response.nCode == 302)
    {   //redirects
        pmlLog(pml::LOG_TRACE) << "clientMessage\tRedirecting";
        auto var = mg_http_get_header(pReply, "Location");
        if(var && var->len > 0)
        {
            m_response.sData.assign(var->ptr, var->len);
        }
        m_eStatus = clientMessage::REDIRECTING;
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
            pmlLog(pml::LOG_WARN) << "clientMessage\tSent binary data but could not open a file to write it to";
        }
        m_eStatus = clientMessage::COMPLETE;
    }

}


void clientMessage::HandleChunkEvent(mg_connection* pConnection, mg_http_message* pReply)
{
    if(m_eStatus == clientMessage::CONNECTED || m_eStatus == clientMessage::SENDING)
    {
        pmlLog(pml::LOG_TRACE) << "clientMessage\tFirst Chunk event";
        GetResponseCode(pReply);
        GetContentHeaders(pReply);

        m_eStatus = clientMessage::CHUNKING;
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
        m_eStatus = clientMessage::COMPLETE;
    }
}


void clientMessage::SetupRedirect()
{
    m_eStatus = clientMessage::CONNECTING;
    if(m_response.sData.find("://") != std::string::npos)
    {
        m_point.second = endpoint(m_response.sData);
    }
    else
    {
        m_point.second = endpoint(m_point.second.Get()+m_response.sData);
    }
}

void clientMessage::HandleErrorEvent(const char* error)
{
    m_response.nCode = ERROR_CONNECTION;
    m_response.sData = error;
    m_eStatus = clientMessage::COMPLETE;

    pmlLog(pml::LOG_TRACE) << "clientMessage\tError event: " << m_response.sData;
}

static void evt_handler(mg_connection* pConnection, int nEvent, void* pEventData, void* pfnData)
{
    clientMessage* pMessage = reinterpret_cast<clientMessage*>(pfnData);

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



const clientResponse& clientMessage::Run(const std::chrono::milliseconds& connectionTimeout, const std::chrono::milliseconds& processTimeout)
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

        if(m_eStatus == clientMessage::REDIRECTING)
        {
            SetupRedirect();
            return Run();
        }
        else if(m_eStatus == clientMessage::COMPLETE)
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

void clientMessage::DoLoop(mg_mgr& mgr)
{
    auto start = std::chrono::system_clock::now();
    while(m_eStatus != clientMessage::REDIRECTING && m_eStatus != clientMessage::COMPLETE)
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


size_t clientMessage::WorkoutDataSize()
{
    if(m_vPostData.empty())
    {
        m_nContentLength = 0;
    }
    else if(m_vPostData.size() == 1)
    {
        if(m_vPostData.back().sFilename.empty())
        {
            m_nContentLength =  m_vPostData.back().sData.length();
        }
        else
        {
            m_ifs.open(m_vPostData.back().sFilename, std::ifstream::ate | std::ifstream::binary);
            if(m_ifs.is_open() == false)
            {

                m_response.nCode = ERROR_FILE_READ;
                m_eStatus = COMPLETE;
                m_nContentLength = 0;
            }
            else
            {
                m_nContentLength = m_ifs.tellg();
                m_ifs.close();
            }
        }
    }
    else
    {
        // @todo sending a multipart so do all that stuff
    }
    return m_nContentLength;
}


void clientMessage::HandleWroteEvent(mg_connection* pConnection, int nBytes)
{

    if(m_vPostData.size() == 1)
    {
        if(m_vPostData.back().sFilename.empty())    //no filename so sending the text
        {
            if(m_eStatus == CONNECTED)
            {
                mg_send(pConnection, m_vPostData.back().sData.c_str(), m_vPostData.back().sData.length());
                m_eStatus = SENDING;
            }
        }
        else
        {
            if(m_eStatus == CONNECTED)
            {
                m_ifs.open(m_vPostData.back().sFilename);
                if(m_ifs.is_open() == false)
                {
                    m_response.nCode = ERROR_FILE_READ;
                    m_eStatus = COMPLETE;
                }
                m_eStatus = SENDING;
            }
            if(m_ifs.is_open())
            {
                char buffer[61440];
                m_ifs.read(buffer, 61440);
                m_nBytesSent += m_ifs.gcount();

                mg_send(pConnection, buffer, m_ifs.gcount());

                if(m_ifs.eof()) //finished sending
                {
                    m_ifs.close();
                }
            }
        }
    }

}
