#include "mongooseserver.h"
#include <iostream>
#include <thread>
#include <initializer_list>
#include "json/json.h"
#include <sstream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <map>
#include "log.h"
#include <chrono>
#include "utils.h"
//#include "uidutils.h"

using namespace std;
using namespace std::placeholders;




static struct mg_http_serve_opts s_ServerOpts;



bool RG_EXPORT operator<(const endpoint& e1, const endpoint& e2)
{
    return (e1.first.Get() < e2.first.Get() || (e1.first.Get() == e2.first.Get() && e1.second.Get() < e2.second.Get()));
}

void mgpmlLog(const void* buff, int nLength, void* param)
{
    std::string str((char*)buff, nLength);
    if(str.length() > 7 && str[4] == '-' && str[7] == '-')
    {   //prefix message

    }
    else
    {
        pmlLog() << "Mongoose: " << str;
    }

}


map<string, string> DecodeQueryString(mg_http_message* pMessage)
{
    map<string, string> mDecode;

    vector<string> vQuery(SplitString(pMessage->query.ptr, '&'));
    for(size_t i = 0; i < vQuery.size(); i++)
    {
        vector<string> vValue(SplitString(vQuery[i], '='));
        if(vValue.size() == 2)
        {
            mDecode.insert(make_pair(vValue[0], vValue[1]));
        }
        else if(vValue.size() == 1)
        {
            mDecode.insert(make_pair(vValue[0], ""));
        }
    }
    return mDecode;
}


static int is_websocket(const struct mg_connection *nc)
{
    return nc->is_websocket;
}



static void ev_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data)
{
    if(nEvent == 0)
    {
        return;
    }

    MongooseServer* pThread = reinterpret_cast<MongooseServer*>(pConnection->fn_data);
    pThread->HandleEvent(pConnection, nEvent, pData);
}


void MongooseServer::EventWebsocketOpen(mg_connection *pConnection, int nEvent, void* pData)
{

    mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pData);

    pmlLog(pml::LOG_INFO) << "EventWebsocketOpen";

}

bool MongooseServer::AuthenticateWebsocket(subscriber& sub, const Json::Value& jsData)
{
    std::lock_guard<std::mutex> lg(m_mutex);

    if(jsData["user"].isString() && jsData["password"].isString())
    {
        auto itUser = m_mUsers.find(userName(jsData["user"].asString()));
        if(itUser != m_mUsers.end() && itUser->second.Get() == jsData["password"].asString())
        {
            auto itEndpoint = m_mWebsocketAuthenticationEndpoints.find(sub.theUrl);
            if(itEndpoint != m_mWebsocketAuthenticationEndpoints.end())
            {
                if(itEndpoint->second(sub.theUrl, itUser->first, sub.peer))
                {
                    pmlLog(pml::LOG_INFO) << "Websocket subscriber: " << sub.peer << " authorized";

                    return true;
                }
                else
                {
                    pmlLog(pml::LOG_WARN) << "Websocket subscriber: " << sub.peer << " not authorized";
                    return false;
                }
            }
            else
            {
                pmlLog(pml::LOG_WARN) << "Websocket subscriber: " << sub.peer << " url: " << sub.theUrl << " has not authorization function";
                return false;
            }
        }
        else
        {
            pmlLog(pml::LOG_WARN) << "Websocket subscriber: " << sub.peer << " User not found or password not correct";
            return false;
        }
    }
    else
    {
        pmlLog(pml::LOG_WARN) << "Websocket subscriber: " << sub.peer << " No user or password sent";
        return false;
    }
}


void MongooseServer::EventWebsocketMessage(mg_connection *pConnection, int nEvent, void* pData)
{

    auto itSubscriber = m_mSubscribers.find(pConnection);
    if(itSubscriber != m_mSubscribers.end())
    {
        mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pData);
        string sMessage(pMessage->data.ptr, pMessage->data.len);

        Json::Value jsData;
        try
        {
            std::stringstream ss;
            ss.str(sMessage);
            ss >> jsData;

            if(jsData["action"].isString())
            {
                if(jsData["action"].asString().substr(0,1) == "_")
                {
                    HandleInternalWebsocketMessage(pConnection, itSubscriber->second, jsData);
                }
                else
                {
                    HandleExternalWebsocketMessage(pConnection, itSubscriber->second, jsData);
                }
            }
            else
            {
                pmlLog(pml::LOG_WARN) << "Websocket messsage: '" << sMessage << "' has incorrect format";
            }
        }
        catch(const Json::RuntimeError& e)
        {
            pmlLog(pml::LOG_ERROR) << "Unable to convert '" << sMessage << "' to JSON: " << e.what();
        }
    }

}


void MongooseServer::HandleInternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData)
{
    if(jsData["action"].asString() == "_authentication")
    {
        sub.bAuthenticated = AuthenticateWebsocket(sub, jsData);
        if(sub.bAuthenticated)
        {
            AddWebsocketSubscriptions(sub, jsData);
        }
        else
        {
            m_mSubscribers.erase(pConnection);
            pConnection->is_closing = 1;
        }
    }
    else
    {
        if(sub.bAuthenticated)
        {
            if(jsData["action"].asString() == "_add")
            {
                AddWebsocketSubscriptions(sub, jsData);
            }
            else if(jsData["action"].asString() == "_remove")
            {
                RemoveWebsocketSubscriptions(sub, jsData);
            }
        }
        else
        {
            pmlLog(pml::LOG_WARN) << "Websocket subscriber: " << sub.peer << " attempted to send data before authenticating. " << " Close the connection";
            m_mSubscribers.erase(pConnection);
            pConnection->is_closing = 1;
        }
    }

}

void MongooseServer::HandleExternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData)
{
    if(sub.bAuthenticated)
    {
        auto itEndpoint = m_mWebsocketMessageEndpoints.find(sub.theUrl);
        if(itEndpoint != m_mWebsocketMessageEndpoints.end())
        {
            itEndpoint->second(sub.theUrl, jsData);
        }
        else
        {
            pmlLog(pml::LOG_WARN) << sub.peer << " has no message endpoint!";
        }
    }
    else
    {
        pmlLog(pml::LOG_WARN) << "Websocket subscriber: " << sub.peer << " attempted to send data before authenticating. " <<  " Close the connection";
        m_mSubscribers.erase(pConnection);
        pConnection->is_closing = 1;
    }
}

void MongooseServer::AddWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData)
{
    pmlLog() << "Websocket subscriber: " << sub.peer << " adding subscriptions " << jsData;

    if(jsData["endpoints"].isArray())
    {
        for(Json::ArrayIndex ai = 0; ai < jsData["endpoints"].size(); ++ai)
        {
            sub.setEndpoints.insert(jsData["endpoints"][ai].asString());
        }
    }
}

void MongooseServer::RemoveWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData)
{
    pmlLog() << "Websocket subscriber: " << sub.peer << " removing subscriptions " << jsData;

    if(jsData["endpoints"].isArray())
    {
        for(Json::ArrayIndex ai = 0; ai < jsData["endpoints"].size(); ++ai)
        {
            sub.setEndpoints.erase(jsData["endpoints"][ai].asString());
        }
    }
}

void MongooseServer::EventWebsocketCtl(mg_connection *pConnection, int nEvent, void* pData)
{
    mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pData);

    std::string sData;

    if((pMessage->flags & WEBSOCKET_OP_TEXT) != 0)
    {
        sData.assign(pMessage->data.ptr, pMessage->data.len);
    }
    pmlLog(pml::LOG_DEBUG) << "Websocket ctl: [" << (int)pMessage->flags << "] " << sData;



    if((pMessage->flags & WEBSOCKET_OP_CLOSE) != 0)
    {
        pmlLog(pml::LOG_DEBUG) << "MongooseServer\tWebsocketCtl - close";
        auto itSub = m_mSubscribers.find(pConnection);
        if(itSub != m_mSubscribers.end())
        {
            auto itEndpoint = m_mWebsocketCloseEndpoints.find(itSub->second.theUrl);
            if(itEndpoint != m_mWebsocketCloseEndpoints.end())
            {
                itEndpoint->second(itSub->second.theUrl, itSub->second.peer);
            }
        }
        m_mSubscribers.erase(pConnection);
    }
}


authorised MongooseServer::CheckAuthorization(mg_http_message* pMessage)
{
    std::lock_guard<std::mutex> lg(m_mutex);

    if(m_mUsers.empty())
    {
        pmlLog(pml::LOG_TRACE) << "CheckAuthorization: none set";
        return std::make_pair(true, userName(""));
    }

    char sUser[255];
    char sPass[255];
    mg_http_creds(pMessage, sUser, 255, sPass, 255);

    auto itUser = m_mUsers.find(userName(sUser));
    if(itUser != m_mUsers.end() && itUser->second.Get() == std::string(sPass))
    {
        pmlLog(pml::LOG_TRACE) << "CheckAuthorization: user,password found";
        return std::make_pair(true, itUser->first);
    }
    else
    {
        pmlLog(pml::LOG_INFO) << "CheckAuthorization: user '" << sUser <<"' ,password '" << sPass << "' not found";
        return std::make_pair(false, userName(""));
    }

}

void MongooseServer::EventHttp(mg_connection *pConnection, int nEvent, void* pData)
{
    mg_http_message* pMessage = reinterpret_cast<mg_http_message*>(pData);


    char decode[6000];
    mg_url_decode(pMessage->uri.ptr, pMessage->uri.len, decode, 6000, 0);

    string sUri(decode);
    if(sUri[sUri.length()-1] == '/')    //get rid of trailling /
    {
        sUri = sUri.substr(0, sUri.length()-1);
    }

    string sMethod(pMessage->method.ptr);
    size_t nSpace = sMethod.find(' ');
    sMethod = sMethod.substr(0, nSpace);

    pmlLog(pml::LOG_DEBUG) << "MongooseServer\tEndpoint: <" << sMethod << ", " << sUri << ">";

    if(CmpNoCase(sMethod, "OPTIONS"))
    {
        SendOptions(pConnection, sUri);
    }
    else
    {
        auto itWsEndpoint = m_mWebsocketAuthenticationEndpoints.find(url(sUri));
        if(itWsEndpoint != m_mWebsocketAuthenticationEndpoints.end())
        {

            mg_ws_upgrade(pConnection, pMessage, nullptr);
            char buffer[256];
            mg_ntoa(&pConnection->peer, buffer, 256);
            std::stringstream ssPeer;
            ssPeer << buffer << ":" << pConnection->peer.port;

            m_mSubscribers.insert(std::make_pair(pConnection, subscriber(url(sUri), ipAddress(ssPeer.str()))));
        }
        else
        {
            auto auth = CheckAuthorization(pMessage);
            if(auth.first == false)
            {
                SendAuthenticationRequest(pConnection);
                return;
            }


            std::string sQuery, sData;
            if(pMessage->body.len > 0)
            {
                sData.assign(pMessage->body.ptr, pMessage->body.len);
            }
            if(pMessage->query.len > 0)
            {
                mg_url_decode(pMessage->query.ptr, pMessage->query.len, decode, 6000, 0);
                sQuery = std::string(decode);
            }

            //find the callback function assigned to the method and url
            auto itCallback = m_mEndpoints.find(endpoint(httpMethod(sMethod),url(sUri)));
            if(itCallback != m_mEndpoints.end())
            {
                DoReply(pConnection, itCallback->second(query(sQuery), postData(sData), url(sUri), auth.second));
            }
            else
            {   //none found so sne a "not found" error
                if(m_sStaticRootDir.empty() == false)
                {
                    mg_http_serve_opts opts = {.root_dir = m_sStaticRootDir.c_str()};
                    mg_http_serve_dir(pConnection, pMessage, &opts);
                }
                else
                {
                    SendError(pConnection, "Not Found", 404);
                }
            }
        }
    }
}

void MongooseServer::HandleEvent(mg_connection *pConnection, int nEvent, void* pData)
{
    switch (nEvent)
    {
        case MG_EV_ACCEPT:
            HandleAccept(pConnection);
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_TRACE) << "MG_EV_WS_OPEN";
            EventWebsocketOpen(pConnection, nEvent, pData);
            break;
        case MG_EV_WS_CTL:
            pmlLog(pml::LOG_TRACE) << "MG_EV_WS_CTL";
            EventWebsocketCtl(pConnection, nEvent, pData);
            break;
        case MG_EV_WS_MSG:
            pmlLog(pml::LOG_TRACE) << "MG_EV_WS_MSG";
            EventWebsocketMessage(pConnection, nEvent, pData);
            break;
        case MG_EV_HTTP_MSG:
            pmlLog(pml::LOG_TRACE) << "MG_EV_HTTP_MSG";
            EventHttp(pConnection, nEvent, pData);
            break;
        case MG_EV_CLOSE:
            pmlLog(pml::LOG_TRACE) << "MG_EV_CLOSE";
            if (is_websocket(pConnection))
            {
                pmlLog(pml::LOG_DEBUG) << "MongooseServer\tWebsocket closed";
                pConnection->fn_data = nullptr;
                m_mSubscribers.erase(pConnection);
            }
            pmlLog(pml::LOG_DEBUG) << "MongooseServer\tDone";
            break;
//        case MG_EV_HTTP_MULTIPART_REQUEST:
//            MultipartBegin(pConnection, reinterpret_cast<http_message*>(pData));
//            break;
//        case MG_EV_HTTP_PART_BEGIN:
//            PartBegin(pConnection, reinterpret_cast<mg_http_multipart_part*>(pData));
//            break;
//        case MG_EV_HTTP_PART_DATA:
//            PartData(pConnection, reinterpret_cast<mg_http_multipart_part*>(pData));
//            break;
//        case MG_EV_HTTP_PART_END:
//            PartEnd(pConnection, reinterpret_cast<mg_http_multipart_part*>(pData));
//            break;
//        case MG_EV_HTTP_MULTIPART_REQUEST_END:
//            MultipartEnd(pConnection, reinterpret_cast<mg_http_multipart_part*>(pData));
//            break;

        case 0:
            break;
    }
}

void MongooseServer::HandleAccept(mg_connection* pConnection)
{
    if(mg_url_is_ssl(m_sServerName.c_str()))
    {
        pmlLog(pml::LOG_DEBUG) << "Accept connection: Turn on TLS";
        struct mg_tls_opts tls_opts;
        tls_opts.ca = NULL;
        tls_opts.srvname.len = 0;
        tls_opts.srvname.ptr = NULL;
        tls_opts.cert = m_sCert.c_str();
        tls_opts.certkey = m_sKey.c_str();
        tls_opts.ciphers = NULL;
        //tls_opts.ciphers = "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256";
        if(mg_tls_init(pConnection, &tls_opts) == 0)
        {
            pmlLog(pml::LOG_ERROR) << "Could not implement TLS";
        }
    }
}


MongooseServer::MongooseServer() :
    m_pConnection(nullptr),
    m_nPollTimeout(100),
    m_loopCallback(nullptr),
    m_bLoop(true),
    m_pThread(nullptr)
{
    #ifdef __WXDEBUG__
    mg_log_set("2");
    mg_log_set_callback(mgpmlLog, NULL);
    #endif // __WXDEBUG__

    m_multipartData.itEndpoint = m_mEndpoints.end();
}

MongooseServer::~MongooseServer()
{
    if(m_pThread)
    {
        m_bLoop = false;
        m_pThread->join();
    }
}

bool MongooseServer::Init(const std::string& sCert, const std::string& sKey, int nPort)
{
    //check for ssl
    m_sKey = sKey;
    m_sCert = sCert;


    char hostname[255];
    gethostname(hostname, 255);
    stringstream ssRewrite;
    ssRewrite << "%80=https://" << hostname;



    s_ServerOpts.root_dir = ".";
// @todo    s_ServerOpts.extra_headers="X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown";

    mg_mgr_init(&m_mgr);

    stringstream ss;

    if(sCert.empty())
    {
        ss << "http://0.0.0.0:";
    }
    else
    {
        ss << "https://0.0.0.0:";
    }
    ss << nPort;
    m_sServerName = ss.str();

    m_pConnection = mg_http_listen(&m_mgr, ss.str().c_str(), ev_handler, nullptr);



    if(m_pConnection)
    {
        m_pConnection->fn_data = reinterpret_cast<void*>(this);

        pmlLog(pml::LOG_INFO) << "Server started: " << ss.str();
        pmlLog(pml::LOG_INFO) << "--------------------------";
        return true;
    }
    else
    {
        pmlLog(pml::LOG_ERROR) << "Could not start webserver";
        return false;
    }
}

void MongooseServer::Run(bool bThread, unsigned int nTimeoutMs)
{
    if(m_pConnection)
    {
        m_nPollTimeout = nTimeoutMs;

        if(bThread)
        {
            m_pThread = std::make_unique<std::thread>(&MongooseServer::Loop, this);
        }
        else
        {
            Loop();
        }
    }
}

void MongooseServer::Loop()
{
    if(m_pConnection)
    {
        int nCount = 0;
        while (m_bLoop)
        {
            auto now = std::chrono::high_resolution_clock::now();

            mg_mgr_poll(&m_mgr, m_nPollTimeout);

            auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()-now.time_since_epoch());

            if(m_loopCallback)
            {
                m_loopCallback(took.count());
            }
            SendWSQueue();

            ++nCount;
        }
        mg_mgr_free(&m_mgr);
    }
}

void MongooseServer::Stop()
{
    m_bLoop = false;
}

bool MongooseServer::AddWebsocketEndpoint(const url& theUrl, std::function<bool(const url&, const userName&, const ipAddress& peer)> funcAuthentication, std::function<bool(const url&, const Json::Value&)> funcMessage, std::function<void(const url&, const ipAddress& peer)> funcClose)
{
    return m_mWebsocketAuthenticationEndpoints.insert(std::make_pair(theUrl, funcAuthentication)).second &&
           m_mWebsocketMessageEndpoints.insert(std::make_pair(theUrl, funcMessage)).second &&
           m_mWebsocketCloseEndpoints.insert(std::make_pair(theUrl, funcClose)).second;
}

bool MongooseServer::AddEndpoint(const endpoint& theEndpoint, std::function<response(const query&, const postData&, const url&, const userName& )> func)
{
    pml::LogStream lg;
    lg << "MongooseServer\t" << "AddEndpoint <" << theEndpoint.first.Get() << ", " << theEndpoint.second.Get() << "> ";
    if(m_mEndpoints.find(theEndpoint) != m_mEndpoints.end())
    {
        lg.SetLevel(pml::LOG_WARN);
        lg << "failed as endpoint already exists";
        return false;
    }

    m_mEndpoints.insert(std::make_pair(theEndpoint, func));
    m_mmOptions.insert(std::make_pair(theEndpoint.second.Get(), theEndpoint.first));
    lg.SetLevel(pml::LOG_TRACE);
    lg << "success";
    return true;
}

bool MongooseServer::DeleteEndpoint(const endpoint& theEndpoint)
{
    m_mmOptions.erase(theEndpoint.second.Get());
    return (m_mEndpoints.erase(theEndpoint) != 0);
}


void MongooseServer::SendError(mg_connection* pConnection, const string& sError, int nCode)
{
    DoReply(pConnection, response(nCode, sError));
}

void MongooseServer::ClearMultipartData()
{
    m_multipartData.itEndpoint = m_mEndpoints.end();
    m_multipartData.mData.clear();
    m_multipartData.mFiles.clear();
    if(m_multipartData.ofs.is_open())
    {
        m_multipartData.ofs.close();
    }
}
/*
bool MongooseServer::MultipartBegin(mg_connection* pConnection, http_message* pMessage)
{
    LogStream lg;
    lg << "Starting upload";

    if(pMessage->message.len > 0)
    {
        lg << string(pMessage->message.ptr) << endl;
    }

    string sUri;
    sUri.assign(pMessage->uri.ptr, pMessage->uri.len);
    string sMethod(pMessage->method.ptr);
    lg << sMethod << std::endl;
    size_t nSpace = sMethod.find(' ');
    sMethod = sMethod.substr(0, nSpace);

    lg << "MongooseServer\tUpload: <" << sMethod << ", " << sUri << ">" << std::endl;

    ClearMultipartData();
    m_multipartData.itEndpoint = m_mEndpoints.find(endpoint(httpMethod(sMethod), url(sUri)));
    if(m_multipartData.itEndpoint == m_mEndpoints.end())
    {
        SendError(pConnection, "Method not allowed.", 405);
        return false;
    }
    else
    {
        return true;
    }
}


bool MongooseServer::PartBegin(mg_connection* pConnection, mg_http_multipart_part* pPart)
{
    pmlLog() << "MongooseServer\tPartBegin: '" << pPart->file_name << "' '" << pPart->var_name << "' " << std::endl;

    if(std::string(pPart->file_name) != "")
    {
        auto pairFile = m_multipartData.mFiles.insert(std::make_pair(std::string(pPart->var_name), "/tmp/temp")); //@todo make unique
        m_multipartData.ofs.open(pairFile.first->second, std::ios::binary);
        if(m_multipartData.ofs.is_open() == false)
        {
            ClearMultipartData();
            SendError(pConnection, "Failed to open a file", 500);
            return false;
        }
    }
    else
    {
        m_multipartData.mData.insert(std::make_pair(pPart->var_name, ""));
    }
    return true;
}

bool MongooseServer::PartData(mg_connection* pConnection, mg_http_multipart_part* pPart)
{
    if(std::string(pPart->file_name) != "" && m_multipartData.ofs.is_open())
    {
        m_multipartData.ofs.write(pPart->data.ptr, pPart->data.len);
        m_multipartData.ofs.flush();
        if(!m_multipartData.ofs)
        {
            ClearMultipartData();
            SendError(pConnection, "Failed to write to file", 500);
            return false;
        }
    }
    else
    {
        auto itData = m_multipartData.mData.find(pPart->var_name);
        if(itData != m_multipartData.mData.end())
        {
            itData->second.append(pPart->data.ptr, pPart->data.len);
        }
        else
        {
            SendError(pConnection, "Failed to store form data", 500);
            return false;
        }
    }
    return true;
}

bool MongooseServer::PartEnd(mg_connection* pConnection, mg_http_multipart_part* pPart)
{
    if(m_multipartData.ofs.is_open())
    {
        m_multipartData.ofs.close();
    }
    return true;
}


bool MongooseServer::MultipartEnd(mg_connection* pConnection, mg_http_multipart_part* pPart)
{
    pmlLog(pml::LOG_DEBUG) << "MongooseServer\tFinished Multipart" << endl;


    Json::Value jsData;

    for(auto pairData : m_multipartData.mData)
    {
        pmlLog() << "MongooseServer\tMultipart: " << pairData.first << "=" << pairData.second << std::endl;
        jsData[pairData.first] = pairData.second;
    }
    Json::Value jsFiles;
    for(auto pairFiles : m_multipartData.mFiles)
    {
        pmlLog() << "MongooseServer\tMultipart Files: " << pairFiles.first << "=" << pairFiles.second << std::endl;
        jsFiles[pairFiles.first] = pairFiles.second;
    }

    Json::Value jsBody;
    jsBody["multipart"]["data"] = jsData;
    jsBody["multipart"]["files"] = jsFiles;

    std::stringstream ss;
    ss << jsBody;

    if(m_multipartData.itEndpoint != m_mEndpoints.end())
    {
        DoReply(pConnection, m_multipartData.itEndpoint->second(query(""), postData(ss.str()), m_multipartData.itEndpoint->first.second));
        return true;
    }
    else
    {
        SendError(pConnection, "Not found", 404);
        return false;
    }
}
*/

void MongooseServer::DoReply(mg_connection* pConnection,const response& theResponse)
{
    std::stringstream ssJson;
    ssJson << theResponse.jsonData;

    pmlLog() << "MongooseServer::DoReply " << theResponse.nHttpCode;
    pmlLog() << "MongooseServer::DoReply " << ssJson.str();



    stringstream ssHeaders;
    ssHeaders << "HTTP/1.1 " << theResponse.nHttpCode << " \r\n"
              << "Content-Type: " << "application/json" << "\r\n"
              << "Content-Length: " << ssJson.str().length() << "\r\n"
              << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
              << "Access-Control-Allow-Origin:*\r\n"
              << "Access-Control-Allow-Methods:GET, PUT, POST, HEAD, OPTIONS, DELETE\r\n"
              << "Access-Control-Allow-Headers:Content-Type, Accept, Authorization\r\n"
              << "Access-Control-Max-AgeL3600\r\n\r\n";

//    mg_http_reply(pConnection, theResponse.nHttpCode, ssHeaders.str().c_str(), "%s", ssJson.str().c_str());
        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        mg_send(pConnection, ssJson.str().c_str(), ssJson.str().length());
}


void MongooseServer::SendOptions(mg_connection* pConnection, const std::string& sUrl)
{
    auto itOption = m_mmOptions.lower_bound(sUrl);
    if(itOption == m_mmOptions.upper_bound(sUrl))
    {
        stringstream ssHeaders;
        ssHeaders << "HTTP/1.1 404\r\n"
                << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
                  << "Access-Control-Allow-Origin:*\r\n"
                  << "Access-Control-Allow-Methods: OPTIONS";

        ssHeaders << "\r\n"
                  << "Content-Length: 0 \r\n"
                  << "Access-Control-Allow-Headers:Content-Type, Accept, Authorization\r\n"
                  << "Access-Control-Max-AgeL3600\r\n\r\n";

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());

    }
    else
    {
        stringstream ssHeaders;
        ssHeaders << "HTTP/1.1 200\r\n"
                << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
                  << "Access-Control-Allow-Origin:*\r\n"
                  << "Access-Control-Allow-Methods: OPTIONS";

        for(; itOption != m_mmOptions.upper_bound(sUrl); ++itOption)
        {
            ssHeaders << ", " << itOption->second.Get();
        }
        ssHeaders << "\r\n"
                  << "Content-Length: 0 \r\n"
                  << "Access-Control-Allow-Headers:Content-Type, Accept, Authorization\r\n"
                  << "Access-Control-Max-AgeL3600\r\n\r\n";

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        pConnection->is_draining = 1;
    }
}


void MongooseServer::SendAuthenticationRequest(mg_connection* pConnection)
{
    stringstream ssHeaders;
    ssHeaders << "HTTP/1.1 401\r\n"
               << "WWW-Authenticate: Basic realm=\"User Visible Realm\"\r\n\r\n";

    mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
    pConnection->is_draining = 1;
}



void MongooseServer::SendWSQueue()
{
    std::lock_guard<std::mutex> lg(m_mutex);

    if(m_pConnection)
    {
        while(m_qWsMessages.empty() == false)
        {
            std::stringstream ssMessage;
            ssMessage << m_qWsMessages.front().second;

            //turn message into array
            char *cstr = new char[ssMessage.str().length() + 1];
            strcpy(cstr, ssMessage.str().c_str());


            for (mg_connection* pConnection = m_pConnection->mgr->conns; pConnection != NULL; pConnection = pConnection->next)
            {
                if(is_websocket(pConnection))
                {
                    auto itSubscriber = m_mSubscribers.find(pConnection);
                    if(itSubscriber != m_mSubscribers.end())
                    {
                        pmlLog(pml::LOG_TRACE) << "Websocket messsage: subscriber: '" << itSubscriber->second.theUrl << "'";

                        if(itSubscriber->second.bAuthenticated) //authenticated
                        {
                            bool bSent(false);
                            for(auto sEndpoint : m_qWsMessages.front().first)
                            {
                                for(auto sSub : itSubscriber->second.setEndpoints)
                                {
                                    if(sSub.length() <= sEndpoint.length() && sEndpoint.substr(0, sSub.length()) == sSub)
                                    {   //has subscribed to something upstream of this endpoint
                                        pmlLog(pml::LOG_TRACE) << "Send websocket message from: " << sEndpoint;
                                        mg_ws_send(pConnection, cstr, strlen(cstr), WEBSOCKET_OP_TEXT);
                                        bSent = true;
                                        break;
                                    }
                                }
                                if(bSent)
                                {
                                    break;
                                }
                            }
                        }
                        else
                        {
                            pmlLog(pml::LOG_TRACE) << itSubscriber->second.theUrl << " not yet authenticated...";
                        }
                    }
                }
            }
            delete[] cstr;
            m_qWsMessages.pop();
        }
    }
}


void MongooseServer::SendWebsocketMessage(const std::set<std::string>& setEndpoints, const Json::Value& jsMessage)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    m_qWsMessages.push(wsMessage(setEndpoints, jsMessage));
}

void MongooseServer::SetLoopCallback(std::function<void(unsigned int)> func)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    m_loopCallback = func;
}


void MongooseServer::AddBAUser(const userName& aUser, const password& aPassword)
{
    std::lock_guard<std::mutex> lg(m_mutex);

    auto ins = m_mUsers.insert(std::make_pair(aUser, aPassword));
    if(ins.second == false)
    {
        ins.first->second = aPassword;
    }
}

void MongooseServer::DeleteBAUser(const userName& aUser)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    m_mUsers.erase(aUser);
}

std::set<endpoint> MongooseServer::GetEndpoints()
{
    std::set<endpoint> setEndpoints;
    for(auto pairEnd : m_mEndpoints)
    {
        setEndpoints.insert(pairEnd.first);
    }
    return setEndpoints;
}
