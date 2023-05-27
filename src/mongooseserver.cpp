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
#include <algorithm>
#include <limits>
#include "threadpool.h"
#include "safequeue.h"


using namespace std::placeholders;
using namespace pml::restgoose;

const std::string DISPOSITION = "Content-Disposition: ";
const std::string NAME = " name=";
const std::string FILENAME = " filename=";

static struct mg_http_serve_opts s_ServerOpts;


bool end_less::operator() (endpoint e1, endpoint e2) const
{
    return std::lexicographical_compare(e1.Get().begin(), e1.Get().end(), e2.Get().begin(), e2.Get().end(), [](unsigned char a, unsigned char b){
                                        return toupper(a) < toupper(b);
                                        });
}


bool is_chunked(struct mg_http_message *hm) {
  struct mg_str needle = mg_str_n("chunked", 7);
  struct mg_str const *te = mg_http_get_header(hm, "Transfer-Encoding");
  return te != nullptr && mg_strstr(*te, needle) != nullptr;
}

void http_delete_chunk(struct mg_connection *c, struct mg_http_message *hm) {
  struct mg_str ch = hm->chunk;
  const char *end = (char *) &c->recv.buf[c->recv.len];
  const char *ce;
  
  if (bool chunked = is_chunked(hm);chunked) 
  {
    ch.len += 4, ch.ptr -= 2;  // \r\n before and after the chunk
    while (ch.ptr > hm->body.ptr && *ch.ptr != '\n') ch.ptr--, ch.len++;
  }
  ce = &ch.ptr[ch.len];
  if (ce < end) memmove((void *) ch.ptr, ce, (size_t) (end - ce));
  c->recv.len -= ch.len;
  if (c->pfn_data != nullptr) c->pfn_data = (char *) c->pfn_data - ch.len;
}


size_t GetNumberOfConnections(mg_mgr& mgr)
{
    size_t nCount = 0;
    mg_connection* pTmp = mgr.conns;
    if(pTmp)
    {
        nCount++;
        while((pTmp = pTmp->next) != nullptr)
        {
            ++nCount;
        }
    }
    return nCount;
}

size_t DoGetNumberOfWebsocketConnections(const mg_mgr& mgr)
{
    size_t nCount = 0;
    mg_connection* pTmp = mgr.conns;
    if(pTmp)
    {
        do
        {
            if(pTmp->is_websocket)
            {
                nCount++;
            }
        }while((pTmp = pTmp->next) != nullptr);
    }
    return nCount;
}

headerValue GetHeader(struct mg_http_message* pMessage, const headerName& name)
{
    headerValue value("");
    if(mg_str const* pStr = mg_http_get_header(pMessage, name.Get().c_str()); pStr && pStr->len > 0)
    {
        value = headerValue(std::string(pStr->ptr, pStr->len));
    }
    return value;
}

std::vector<partData> CreatePartData(const mg_str& str, const headerValue& contentType)
{
    pmlLog(pml::LOG_TRACE) << "CreatePartData " << contentType;
    if(contentType.Get() != "application/x-www-form-urlencoded")
    {
        return {partData(partName(""), textData(std::string(str.ptr, str.ptr+str.len)))};
    }
    else
    {
        auto vSplit = SplitString(std::string(str.ptr, str.ptr+str.len), '&');
        std::vector<partData> vData;
        vData.reserve(vSplit.size());

        for(const auto& sEntry : vSplit)
        {
            auto nPos = sEntry.find('=');
            if(nPos == std::string::npos)
            {
                vData.emplace_back(partData(partName(""), textData(sEntry)));
            }
            else
            {
                vData.emplace_back(partData(partName(sEntry.substr(0,nPos)), textData(sEntry.substr(nPos+1))));
            }
        }
        return vData;
    }
}

partData CreatePartData(const mg_http_part& mgpart, const headerValue& )
{
    partData part(partName(std::string(mgpart.name.ptr, mgpart.name.len)), textData(std::string(mgpart.filename.ptr, mgpart.filename.len)), CreateTmpFileName("/tmp/"));

    if(part.filepath.Get().empty() == false)
    {
        std::ofstream ofs;
        ofs.open(part.filepath.Get());
        if(ofs.is_open())
        {
            ofs.write(mgpart.body.ptr, mgpart.body.len);
            ofs.close();
        }
    }
    else
    {
        part.data = textData(std::string(mgpart.body.ptr, mgpart.body.ptr+mgpart.body.len));
    }
    return part;
}

bool caseInsLess(const std::string& s1, const std::string& s2)
{
    return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), [](unsigned char a, unsigned char b){ return toupper(a) < toupper(b); });
}


bool RG_EXPORT operator<(const methodpoint& e1, const methodpoint& e2)
{
    return (e1.first.Get() < e2.first.Get() || (e1.first.Get() == e2.first.Get() && caseInsLess(e1.second.Get(), e2.second.Get())));
}

static void mgpmlLog(char ch, void*)
{
    static pml::LogStream ls;
    ls.SetLevel(pml::LOG_TRACE);
    if(ch != '\n')
    {
        ls << ch;
    }
    else
    {
        ls << std::endl;
    }
}


std::string DecodeQueryString(mg_http_message const* pMessage)
{
    if(pMessage->query.len > 0)
    {
        char decode[6000];
        mg_url_decode(pMessage->query.ptr, pMessage->query.len, decode, 6000, 0);
        return std::string(decode);
    }
    return std::string("");
}

query ExtractQuery(mg_http_message const* pMessage)
{
    query mDecode;

    auto sQuery = DecodeQueryString(pMessage);

    auto vQuery = SplitString(sQuery, '&');
    for(const auto& sParam : vQuery)
    {
        auto vValue = SplitString(sParam, '=');
        if(vValue.size() == 2)
        {
            mDecode.try_emplace(queryKey(vValue[0]), queryValue(vValue[1]));
        }
        else if(vValue.size() == 1)
        {
            mDecode.try_emplace(queryKey(vValue[0]), queryValue(""));
        }
    }
    return mDecode;
}


static int is_websocket(const struct mg_connection* nc)
{
    return nc->is_websocket;
}



void ev_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data)
{
    if(nEvent == 0 || fn_data == nullptr)
    {
        return;
    }

    auto pThread = reinterpret_cast<MongooseServer*>(pConnection->fn_data);

    pThread->HandleEvent(pConnection, nEvent, pData);
}

void pipe_handler(mg_connection* pConnection, int nEvent, void* pData, void* fn_data)
{
    if(nEvent == MG_EV_READ)
    {

        auto pThread = reinterpret_cast<MongooseServer*>(fn_data);
        pThread->SendWSQueue();
    }
}

MongooseServer::httpchunks::~httpchunks()=default;

void MongooseServer::EventWebsocketOpen(mg_connection const*, int , void* )
{

    pmlLog(pml::LOG_TRACE) << "RestGoose:Server\tEventWebsocketOpen";

}

bool MongooseServer::AuthenticateWebsocket(const subscriber& sub, const Json::Value& jsData)
{
    std::scoped_lock lg(m_mutex);

    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tAuthenticateWebsocket";
    if(m_tokenCallback)
    {
        return AuthenticateWebsocketBearer(sub, jsData);
    }
    else if(m_mUsers.empty() == false)
    {
        return AuthenticateWebsocketBasic(sub,jsData);
    }
    else
    {
        return true;
    }
}

bool MongooseServer::AuthenticateWebsocketBasic(const subscriber& sub, const Json::Value& jsData)
{
    if(jsData.isMember("user") && jsData["user"].isString() && jsData.isMember("password") && jsData["password"].isString())
    {
        if(auto itUser = m_mUsers.find(userName(jsData["user"].asString()));itUser != m_mUsers.end() && itUser->second.Get() == jsData["password"].asString())
        {
            if(auto itEndpoint = m_mWebsocketAuthenticationEndpoints.find(sub.theEndpoint); itEndpoint != m_mWebsocketAuthenticationEndpoints.end())
            {
                if(itEndpoint->second(sub.theEndpoint, sub.queryParams, itUser->first, sub.peer))
                {
                    pmlLog(pml::LOG_INFO) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " authorized";

                    return true;
                }
                else
                {
                    pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " not authorized";
                    return false;
                }
            }
            else
            {
                pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " endpoint: " << sub.theEndpoint << " has not authorization function";
                return false;
            }
        }
        else
        {
            pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " User not found or password not correct";
            return false;
        }
    }
    else
    {
        pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " No user or password sent";
        return false;
    }
}

bool MongooseServer::AuthenticateWebsocketBearer(const subscriber& sub, const Json::Value& jsData)
{
    std::string sToken;
    if(m_bAuthenticateWSViaQuery)
    {
        if(auto itToken = sub.queryParams.find(queryKey("access_token")); itToken != sub.queryParams.end())
        {
            sToken = itToken->second.Get();
        }
    }
    else if(jsData["bearer"].isString())
    {
        sToken = jsData["bearer"].asString();
    }
    else
    {
        pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " No bearer token sent";
        return false;
    }
    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tAuthenticateWebsocketBearer token=" << sToken;

    if(auto itEndpoint = m_mWebsocketAuthenticationEndpoints.find(sub.theEndpoint); itEndpoint != m_mWebsocketAuthenticationEndpoints.end())
    {
        if(itEndpoint->second(sub.theEndpoint, sub.queryParams, userName(sToken), sub.peer))
        {
            pmlLog(pml::LOG_INFO) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " authorized";
            return true;
        }
        else
        {
            pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " not authorized";
            return false;
        }
    }
    else
    {
        pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " endpoint: " << sub.theEndpoint << " has not authorization function";
        return false;
    }


}



void MongooseServer::EventWebsocketMessage(mg_connection* pConnection, int, void* pData )
{
    pmlLog(pml::LOG_TRACE) << "RestGoose::Server\tEventWebsocketMessage";

    auto itSubscriber = m_mSubscribers.find(pConnection);
    if(itSubscriber != m_mSubscribers.end())
    {
        auto pMessage = reinterpret_cast<mg_ws_message*>(pData);
        std::string sMessage(pMessage->data.ptr, pMessage->data.len);

        Json::Value jsData;
        try
        {
            std::stringstream ss;
            ss.str(sMessage);
            ss >> jsData;

            if(jsData["action"].isString() && jsData["action"].asString().substr(0,1) == "_")
            {
                HandleInternalWebsocketMessage(pConnection, itSubscriber->second, jsData);
            }
            else
            {
                HandleExternalWebsocketMessage(pConnection, itSubscriber->second, jsData);
            }

        }
        catch(const Json::RuntimeError& e)
        {
            pmlLog(pml::LOG_ERROR) << "RestGoose:Server\tUnable to convert '" << sMessage << "' to JSON: " << e.what();
        }
    }

}

void MongooseServer::DoWebsocketAuthentication(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData)
{

    sub.bAuthenticated = AuthenticateWebsocket(sub, jsData);
    if(sub.bAuthenticated)
    {
        pmlLog(pml::LOG_TRACE) << "RestGoose::Server\tHandleInternalWebsocketMessage: Authenticated";
        AddWebsocketSubscriptions(sub, jsData);
    }
    else
    {
        pmlLog(LOG_DEBUG) << "RestGoose:Server\ttWebsocket subscriber not authenticated: close";
        m_mSubscribers.erase(pConnection);
        pConnection->is_closing = 1;
    }
}

void MongooseServer::HandleInternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData)
{
    if(jsData["action"].asString() == "_authentication")
    {
        DoWebsocketAuthentication(pConnection, sub, jsData);
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
            pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " attempted to send data before authenticating. " << " Close the connection";
            m_mSubscribers.erase(pConnection);
            pConnection->is_closing = 1;
        }
    }

}

void MongooseServer::HandleExternalWebsocketMessage(mg_connection* pConnection, const subscriber& sub, const Json::Value& jsData)
{
    pmlLog(pml::LOG_TRACE) << "RestGoose::Server\tHandleExternalWebsocketMessage";
    if(sub.bAuthenticated)
    {
        auto itEndpoint = m_mWebsocketMessageEndpoints.find(sub.theEndpoint);
        if(itEndpoint != m_mWebsocketMessageEndpoints.end())
        {
            itEndpoint->second(sub.theEndpoint, jsData);
        }
        else
        {
            pmlLog(pml::LOG_WARN) << "RestGoose:Server\t" << sub.peer << " has no message methodpoint!";
        }
    }
    else
    {
        pmlLog(pml::LOG_WARN) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " attempted to send data before authenticating. " <<  " Close the connection";
        m_mSubscribers.erase(pConnection);
        pConnection->is_closing = 1;
    }
}

void MongooseServer::AddWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData)
{
    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\ttWebsocket subscriber: " << sub.peer << " adding subscriptions " << jsData;

    if(jsData["endpoints"].isArray())
    {
        for(Json::ArrayIndex ai = 0; ai < jsData["endpoints"].size(); ++ai)
        {
            sub.setEndpoints.emplace(jsData["endpoints"][ai].asString());
        }
    }
}

void MongooseServer::RemoveWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData)
{
    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tWebsocket subscriber: " << sub.peer << " removing subscriptions " << jsData;

    if(jsData["endpoints"].isArray())
    {
        for(Json::ArrayIndex ai = 0; ai < jsData["endpoints"].size(); ++ai)
        {
            sub.setEndpoints.erase(endpoint(jsData["endpoints"][ai].asString()));
        }
    }
}

void MongooseServer::EventWebsocketCtl(mg_connection *pConnection, int nEvent, void* pData)
{
    auto pMessage = reinterpret_cast<mg_ws_message*>(pData);
    switch((pMessage->flags&15))
    {
        case WEBSOCKET_OP_PONG:
            {
                auto itSub = m_mSubscribers.find(pConnection);
                if(itSub != m_mSubscribers.end())
                {
                    itSub->second.bPonged = true;
                }
            }
            break;
        case WEBSOCKET_OP_TEXT:
            {
                std::string sData;
                sData.assign(pMessage->data.ptr, pMessage->data.len);

                pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tWebsocket ctl: [" << (int)pMessage->flags << "] " << sData;
            }
            break;
        case WEBSOCKET_OP_CLOSE:
            {
                pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tWebsocketCtl - close";
                CloseWebsocket(pConnection);
            }
            break;
    }
}

void MongooseServer::CloseWebsocket(mg_connection* pConnection)
{
    auto itSub = m_mSubscribers.find(pConnection);
    if(itSub != m_mSubscribers.end())
    {
        auto itEndpoint = m_mWebsocketCloseEndpoints.find(itSub->second.theEndpoint);
        if(itEndpoint != m_mWebsocketCloseEndpoints.end())
        {
            itEndpoint->second(itSub->second.theEndpoint, itSub->second.peer);
        }
    }
    m_mSubscribers.erase(pConnection);
}


authorised MongooseServer::CheckAuthorization(mg_http_message* pMessage)
{
    //check if the endpoint is one we've unprotected
    auto thePoint = GetMethodPoint(pMessage);

    if(MethodPointUnprotected(thePoint))
    {
        return std::make_pair(true, userName(""));
    }

    if(m_tokenCallback)
    {
        return CheckAuthorizationBearer(thePoint, pMessage);
    }
    else if(m_mUsers.empty() == false)
    {
        return CheckAuthorizationBasic(pMessage);
    }
    else
    {
        return std::make_pair(true, userName(""));
    }
}

authorised MongooseServer::CheckAuthorizationBasic(mg_http_message* pMessage)
{
    std::lock_guard<std::mutex> lg(m_mutex);

    char sUser[255];
    char sPass[255];
    mg_http_creds(pMessage, sUser, 255, sPass, 255);

    auto itUser = m_mUsers.find(userName(sUser));
    if(itUser != m_mUsers.end() && itUser->second.Get() == std::string(sPass))
    {
        return std::make_pair(true, itUser->first);
    }
    else
    {
        pmlLog(pml::LOG_INFO) << "RestGoose:Server\tCheckAuthorization: user '" << sUser <<" with given password not found";
        return std::make_pair(false, userName(""));
    }
}

authorised MongooseServer::CheckAuthorizationBearer(const methodpoint& thePoint, mg_http_message* pMessage)
{
    std::lock_guard<std::mutex> lg(m_mutex);

    char sBearer[65535];
    char sPass[255];
    mg_http_creds(pMessage, sBearer, 65535, sPass, 255);
    
    if(m_tokenCallback(thePoint, sPass))
    {
        return std::make_pair(true, userName(sPass));
    }
    else
    {
        return std::make_pair(false, userName(""));
    }

}

methodpoint MongooseServer::GetMethodPoint(mg_http_message* pMessage) const
{
    char decode[6000];
    mg_url_decode(pMessage->uri.ptr, pMessage->uri.len, decode, 6000, 0);

    std::string sUri(decode);
    pmlLog(pml::LOG_DEBUG) << "MongooseServer\tGetMethodPoint: " << sUri;

    if(sUri[sUri.length()-1] == '/')    //get rid of trailling /
    {
        sUri = sUri.substr(0, sUri.length()-1);
    }

    //@todo possibly allow user to decide if should be case insensitive or not
    //transform(sUri.begin(), sUri.end(), sUri.begin(), ::tolower);

    //remove any double /
    std::string sPath;
    char c(0);
    for(size_t i = 0; i < sUri.length(); i++)
    {
        if(sUri[i] != c || c != '/')
        {
            sPath += sUri[i];
        }
        c = sUri[i];
    }
    sUri = sPath;

    std::string sMethod(pMessage->method.ptr);
    size_t nSpace = sMethod.find(' ');
    sMethod = sMethod.substr(0, nSpace);
    return methodpoint(httpMethod(sMethod), endpoint(sUri));

}

void MongooseServer::HandleFirstChunk(httpchunks& chunk, mg_connection* pConnection, mg_http_message* pMessage)
{
    pmlLog(pml::LOG_TRACE) << "Restgoose:Server\tHandleFirstChunk";


    auto auth = CheckAuthorization(pMessage);
    if(auth.first == false)
    {
        pmlLog(pml::LOG_TRACE) << "Restgoose:Server\tHandleFirstChunk: SendAuthentication...";
        SendAuthenticationRequest(pConnection);
    }
    else
    {
        chunk.theQuery = ExtractQuery(pMessage);
        chunk.theUser = auth.second;
        chunk.thePoint = GetMethodPoint(pMessage);
        //find the callback function assigned to the method and endpoint
        auto itCallback = m_mEndpoints.find(chunk.thePoint);
        if(itCallback != m_mEndpoints.end())
        {
            chunk.pCallback = itCallback->second;
        }
        //@todo should we terminate the connection if the callback is not found?? How do we do this smoothly??

        auto contents = mg_http_get_header(pMessage, "Content-Type");
        if(contents && contents->len > 0)
        {
            chunk.contentType.Get().assign(contents->ptr, contents->len);
        }

        if(chunk.contentType.Get().find("multipart") != std::string::npos)
        {
            WorkoutBoundary(chunk);
        }
        else
        {
            chunk.vParts.push_back(partData());
            chunk.vParts.back().filepath = CreateTmpFileName("/tmp/");
            chunk.vParts.back().data = textData(chunk.vParts.back().filepath.Get());
            chunk.pofs = std::make_shared<std::ofstream>(chunk.vParts.back().filepath.Get());
            if(chunk.pofs->is_open() == false)
            {
                chunk.pofs = nullptr;
                pmlLog(pml::LOG_WARN) << "RestGoose:Server\tCould not create temp file '" << chunk.vParts.back().filepath << "' for upload";
            }
        }

        auto totalSize = mg_http_get_header(pMessage, "Content-Length");
        if(totalSize && totalSize->len > 0)
        {
            try
            {
                chunk.nTotalSize = std::stoul(std::string(totalSize->ptr, totalSize->len));
            }
            catch(const std::exception& e)
            {
                pmlLog(pml::LOG_WARN) << "RestGoose:Server\tCould not decode message length";
            }
        }
        pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tFirst chunk: " << chunk.contentType << "\t" << chunk.nTotalSize << " bytes";
    }
}

void MongooseServer::WorkoutBoundary(httpchunks& chunk)
{
    auto vSplit = SplitString(chunk.contentType.Get(), ';');
    if(vSplit.size() > 1)
    {
        if(vSplit[1].find("boundary") != std::string::npos)
        {
            chunk.sBoundary = "\r\n--"+vSplit[1].substr(10)+"\r\n";
            chunk.sBoundaryLast = "\r\n--"+vSplit[1].substr(10)+"--\r\n";
            chunk.vBuffer.reserve(chunk.sBoundaryLast.length());
            chunk.vBuffer.push_back('\r');
            chunk.vBuffer.push_back('\n');
        }
    }
}

void MongooseServer::EventHttpChunk(mg_connection *pConnection, void* pData)
{
    auto pMessage = reinterpret_cast<mg_http_message*>(pData);

    //Mongoose now fires a Chunk event whether the message is chunked encoded or not. So double check here
    auto chunked = mg_http_get_header(pMessage, "Transfer-Encoding");
    if(!chunked || chunked->len == 0 || std::string(chunked->ptr) != "chunked")
    {
        return;
    }

    auto ins  = m_mChunks.insert({pConnection, httpchunks()});
    if(ins.second)
    {
        HandleFirstChunk(ins.first->second, pConnection, pMessage);
    }

    //if the content type is multipart then we try to extract
    if(ins.first->second.contentType.Get().find("multipart") != std::string::npos)
    {
        HandleMultipartChunk(ins.first->second, pMessage);
    }
    else
    {
        HandleGenericChunk(ins.first->second, pMessage);
    }

    ins.first->second.nCurrentSize += pMessage->chunk.len;
    if(pMessage->chunk.len == 0 && ins.first->second.nCurrentSize >= ins.first->second.nTotalSize)
    {   //received all the data
        HandleLastChunk(ins.first->second, pConnection);
    }
    http_delete_chunk(pConnection, pMessage);
}

void MongooseServer::HandleGenericChunk(httpchunks& chunk, mg_http_message* pMessage)
{
    if(chunk.pofs)
    {
        chunk.pofs->write(pMessage->chunk.ptr, pMessage->chunk.len);
        chunk.pofs->flush();
    }
}
void MongooseServer::HandleMultipartChunk(httpchunks& chunk, mg_http_message* pMessage)
{
    for(size_t i = 0; i < pMessage->chunk.len; i++)
    {
        if(chunk.ePlace == httpchunks::BOUNDARY)
        {
            MultipartChunkBoundary(chunk, pMessage->chunk.ptr[i]);
        }
        else
        {
            MultipartChunkHeader(chunk, pMessage->chunk.ptr[i]);
        }
    }

    if(chunk.pofs)
    {
        chunk.pofs->flush();    //write to disk

    }
}

void MongooseServer::HandleLastChunk(httpchunks& chunk, mg_connection* pConnection)
{
    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tAll chunks received. Now do something with them..." << chunk.nCurrentSize << "/" << chunk.nTotalSize;
    chunk.vBuffer.clear();
    if(chunk.pofs)
    {
        chunk.pofs->close();
        chunk.pofs = nullptr;
    }
    if(chunk.pCallback.first == nullptr)
    {
        pmlLog(pml::LOG_ERROR) << "RestGoose:Server\tSomeone uploaded a big file to a non allowed endpoint";
        //@todo in the end we shouldn't get here.
        //for now remove any files that were uploaded
        for(auto data : chunk.vParts)
        {
            if(data.filepath.Get().empty() == false)
            {
                remove(data.filepath.Get().c_str());
            }
        }
    }
    else
    {
        if(chunk.pCallback.second == false)
        {
            DoReply(pConnection, chunk.pCallback.first(chunk.theQuery, chunk.vParts, chunk.thePoint.second, chunk.theUser));
        }
        else
        {
            DoReplyThreaded(pConnection, chunk.theQuery, chunk.vParts, chunk.thePoint, chunk.theUser);
        }
    }
    m_mChunks.erase(pConnection);
}


void MongooseServer::MultipartChunkBoundary(httpchunks& chunk, char c)
{
    if(c == chunk.sBoundaryLast[chunk.vBuffer.size()])
    {
        chunk.vBuffer.push_back(c);
        if(chunk.vBuffer.size() == chunk.sBoundaryLast.length())
        {
            MultipartChunkLastBoundaryFound(chunk, c);
        }
    }
    else if(c == chunk.sBoundary[chunk.vBuffer.size()])
    {
        chunk.vBuffer.push_back(c);
        if(chunk.vBuffer.size() == chunk.sBoundary.length())
        {
            MultipartChunkBoundaryFound(chunk, c);
        }
    }
    else
    {
        MultipartChunkBoundarySearch(chunk, c);
    }
}

void MongooseServer::MultipartChunkBoundaryFound(httpchunks& chunk, char c)
{
    if(chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().filepath.Get().empty() == false && chunk.pofs)
        {
            pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tClose file" << chunk.vParts.back().filepath;
            chunk.pofs->close();
            chunk.pofs = nullptr;
        }
        else
        {
            pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tName=" << chunk.vParts.back().name << "\tData=" << chunk.vParts.back().data;
        }
    }

    //clear the buffer
    chunk.vBuffer.clear();
    //set the checking to find the header data now
    chunk.ePlace = httpchunks::HEADER;

    //create our partData structure and add the first char of the header
    chunk.vParts.push_back(partData());
    chunk.vParts.back().sHeader += c;
}

void MongooseServer::MultipartChunkLastBoundaryFound(httpchunks& chunk, char c)
{

    if(chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().filepath.Get().empty() == false && chunk.pofs)
        {
            chunk.pofs->close();
            chunk.pofs = nullptr;
        }
    }
    chunk.vBuffer.clear();
}

void MongooseServer::MultipartChunkBoundarySearch(httpchunks& chunk, char c)
{

    //store the buffered data before clearing it
    if(chunk.vBuffer.empty() == false && chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().filepath.Get().empty())
        {
            chunk.vParts.back().data= textData(chunk.vParts.back().data.Get()+std::string(chunk.vBuffer.begin(), chunk.vBuffer.end()));
        }
        else if(chunk.pofs)
        {
            chunk.pofs->write(chunk.vBuffer.data(), chunk.vBuffer.size());
        }
    }
    chunk.vBuffer.clear();

    if(c == chunk.sBoundaryLast[0])
    {   //start the matching process again
        chunk.vBuffer.push_back(c);
    }
    else if(chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().filepath.Get().empty())
        {
            chunk.vParts.back().data.Get() += c;
        }
        else if(chunk.pofs)
        {
            *(chunk.pofs) << c;
        }
    }
}

void MongooseServer::MultipartChunkHeader(httpchunks& chunk, char c)
{
    chunk.vParts.back().sHeader += c;
    if(chunk.vParts.back().sHeader.length() >= 4 && chunk.vParts.back().sHeader.substr(chunk.vParts.back().sHeader.length()-4) == "\r\n\r\n")
    {
        auto vSplit = SplitString(chunk.vParts.back().sHeader, '\n');
        for(auto sLine : vSplit)
        {
            if(sLine.length() > DISPOSITION.length() && sLine.substr(0, DISPOSITION.length()) == DISPOSITION)
            {
                auto vSplit2 = SplitString(sLine.substr(DISPOSITION.length()), ';');
                for(auto sPart : vSplit2)
                {
                    if(sPart.length() > NAME.length() && sPart.substr(0, NAME.length()) == NAME)
                    {
                        auto nStart = sPart.find('"')+1;
                        auto nEnd = sPart.find('"', nStart);

                        chunk.vParts.back().name =  partName(sPart.substr(nStart, nEnd-nStart));

                    }
                    else if(sPart.length() > FILENAME.length() && sPart.substr(0, FILENAME.length()) == FILENAME)
                    {
                        auto nStart = sPart.find('"')+1;
                        auto nEnd = sPart.find('"', nStart);

                        chunk.vParts.back().data =  textData(sPart.substr(nStart, nEnd-nStart));
                        chunk.vParts.back().filepath =CreateTmpFileName("/tmp/");

                        chunk.pofs = std::make_shared<std::ofstream>(chunk.vParts.back().filepath.Get());
                        if(chunk.pofs->is_open() == false)
                        {
                            chunk.pofs = nullptr;
                            pmlLog(pml::LOG_WARN) << "RestGoose:Server\tMultipart upload - Could not open file '" << chunk.vParts.back().data << "'";
                        }

                    }
                }
            }
        }

        chunk.ePlace = httpchunks::BOUNDARY;
    }
}

void MongooseServer::EventHttp(mg_connection *pConnection, int nEvent, void* pData)
{
    auto pMessage = reinterpret_cast<mg_http_message*>(pData);

    auto thePoint = GetMethodPoint(pMessage);
    auto content = mg_http_get_header(pMessage, "Content-Type");


    std::string sContents;
    if(content && content->len > 0)
    {
        sContents = std::string(content->ptr, content->len);
    }

    if(CmpNoCase(thePoint.first.Get(), "OPTIONS"))
    {
        SendOptions(pConnection, thePoint.second);
    }
    else if(InApiTree(thePoint.second))
    {

        pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\t'" << thePoint.second << "' in Api tree";
        auto itWsEndpoint = m_mWebsocketAuthenticationEndpoints.find(thePoint.second);
        if(itWsEndpoint != m_mWebsocketAuthenticationEndpoints.end())
        {
            EventHttpWebsocket(pConnection, pMessage, thePoint.second);
        }
        else if(sContents.find("multipart") != std::string::npos)
        {
            EventHttpApiMultipart(pConnection, pMessage, thePoint);
        }
        else
        {
            EventHttpApi(pConnection, pMessage, thePoint);
        }
    }
    else
    {
        pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\t'" << thePoint.second << "' not in Api tree";
        auto auth = CheckAuthorization(pMessage);
        if(auth.first == false)
        {
            SendAuthenticationRequest(pConnection);
            return;
        }
        //none found so sne a "not found" error
        if(m_sStaticRootDir.empty() == false)
        {
            mg_http_serve_opts opts = {.root_dir = m_sStaticRootDir.c_str()};
            mg_http_serve_dir(pConnection, pMessage, &opts);
        }
    }
}

void MongooseServer::EventHttpWebsocket(mg_connection *pConnection, mg_http_message* pMessage, const endpoint& uri)
{
    pmlLog(pml::LOG_TRACE) << "RestGoose::Server\tEventHttpWebsocket";

    mg_ws_upgrade(pConnection, pMessage, nullptr);

    //create the peer address
    char buffer[256];
    mg_snprintf(buffer, sizeof(buffer), "%M", mg_print_ip, &pConnection->rem);

    std::stringstream ssPeer;
    ssPeer << buffer << ":" << pConnection->rem.port;

    auto itSub = m_mSubscribers.insert(std::make_pair(pConnection, subscriber(uri, ipAddress(ssPeer.str()), ExtractQuery(pMessage)))).first;
    itSub->second.setEndpoints.insert(uri);

    if(m_mUsers.empty() && m_tokenCallback == nullptr)
    {   //if we have not set up any users then we are not using authentication so we don't need to authenticate the websocket
        itSub->second.bAuthenticated = true;
        pmlLog(pml::LOG_TRACE) << "RestGoose::Server\tEventHttpWebsocket: Authenticated";
    }
    else if(m_tokenCallback && m_bAuthenticateWSViaQuery)
    {   //we are using bearer token and passing it to the websocket via the query string
        DoWebsocketAuthentication(pConnection, itSub->second, Json::Value(Json::nullValue));
    }

}

const ipAddress& MongooseServer::GetCurrentPeer(bool bIncludePort)
{
    return bIncludePort ? m_lastPeerAndPort : m_lastPeer;

}

void MongooseServer::EventHttpApi(mg_connection *pConnection, mg_http_message* pMessage, const methodpoint& thePoint)
{
    auto auth = CheckAuthorization(pMessage);
    if(auth.first == false)
    {
        SendAuthenticationRequest(pConnection);
    }
    else
    {

        char buffer[256];
        mg_snprintf(buffer, sizeof(buffer), "%M", mg_print_ip, &pConnection->rem);
        std::stringstream ssPeer;
        ssPeer << buffer;
        m_lastPeer = ipAddress(ssPeer.str());
        ssPeer << ":" << pConnection->rem.port;

        m_lastPeerAndPort = ipAddress(ssPeer.str());



        //find the callback function assigned to the method and endpoint
        auto itCallback = m_mEndpoints.find(thePoint);
        if(itCallback != m_mEndpoints.end())
        {
            if(itCallback->second.second == false)
            {
                DoReply(pConnection, itCallback->second.first(ExtractQuery(pMessage), CreatePartData(pMessage->body, GetHeader(pMessage, headerName("Content-Type"))), thePoint.second, auth.second));
            }
            else
            {
                DoReplyThreaded(pConnection, ExtractQuery(pMessage), CreatePartData(pMessage->body, GetHeader(pMessage, headerName("Content-Type"))), thePoint, auth.second);
            }
        }
        else if(m_callbackNotFound)
        {
            DoReply(pConnection, m_callbackNotFound(thePoint.first, ExtractQuery(pMessage), {}, thePoint.second, auth.second));
        }
        else
        {
            SendError(pConnection, "Not Found", 404);
        }
    }
}

void MongooseServer::DoReplyThreaded(mg_connection* pConnection,const query& theQuery, const std::vector<partData>& theData, const methodpoint& thePoint, const userName& theUser)
{
    m_mConnectionQueue.try_emplace(pConnection, thread_safe_queue<response>());
    ThreadPool::Get().Submit([this, pConnection, theQuery, theData, thePoint, theUser]()
    {
        if(auto itCallback = m_mEndpoints.find(thePoint); itCallback != m_mEndpoints.end())
        {
            auto theResponse = itCallback->second.first(theQuery, theData, thePoint.second, theUser);
            auto itQueue = m_mConnectionQueue.find(pConnection);
            if(itQueue != m_mConnectionQueue.end())
            {
                itQueue->second.push(theResponse);
            }
        }
    });
}

void MongooseServer::EventHttpApiMultipart(mg_connection *pConnection, mg_http_message* pMessage, const methodpoint& thePoint)
{
    auto auth = CheckAuthorization(pMessage);
    if(auth.first == false)
    {
        SendAuthenticationRequest(pConnection);
    }
    else
    {
        //find the callback function assigned to the method and endpoint
        auto itCallback = m_mEndpoints.find(thePoint);
        if(itCallback != m_mEndpoints.end())
        {
            std::vector<partData> vData;
            struct mg_http_part part;
            size_t nOffset=0;
            while((nOffset = mg_http_next_multipart(pMessage->body, nOffset, &part)) > 0)
            {
                vData.push_back(CreatePartData(part, GetHeader(pMessage, headerName("Content-Type"))));
            }
            
            if(itCallback->second.second == false)
            {
                DoReply(pConnection, itCallback->second.first(ExtractQuery(pMessage), vData, thePoint.second, auth.second));
            }
            else
            {
                DoReplyThreaded(pConnection, ExtractQuery(pMessage), vData, thePoint, auth.second);
            }
        }
        else if(m_callbackNotFound)
        {
            DoReply(pConnection, m_callbackNotFound(thePoint.first, ExtractQuery(pMessage), {}, thePoint.second, auth.second));
        }
        else
        {
            SendError(pConnection, "Not Found", 404);
        }
    }
}


void MongooseServer::HandleEvent(mg_connection *pConnection, int nEvent, void* pData)
{

    switch (nEvent)
    {
        case MG_EV_ERROR:
            {
                pmlLog(pml::LOG_ERROR) << reinterpret_cast<char*>(pData);
            }
            break;
        case MG_EV_OPEN:
            pmlLog(pml::LOG_DEBUG) << "MongooseServer\tHandleOpen";
            HandleOpen(pConnection);
            break;
        case MG_EV_ACCEPT:
            pmlLog(pml::LOG_DEBUG) << "MongooseServer\tHandleAccept";
            HandleAccept(pConnection);
            break;
        case MG_EV_WS_OPEN:
            pmlLog(pml::LOG_DEBUG) << "MongooseServer\tHandleWebsocketOpen";
            EventWebsocketOpen(pConnection, nEvent, pData);
            break;
        case MG_EV_WS_CTL:
            pmlLog(pml::LOG_TRACE) << "MongooseServer\tHandleWebsocketCtl";
            EventWebsocketCtl(pConnection, nEvent, pData);
            break;
        case MG_EV_WS_MSG:
            pmlLog(pml::LOG_DEBUG) << "MongooseServer\tHandleWebsocketMsg";
            EventWebsocketMessage(pConnection, nEvent, pData);
            break;
        case MG_EV_HTTP_MSG:
            pmlLog(pml::LOG_DEBUG) << "MongooseServer\tHandleHTTPMsg";
            pmlLog(pml::LOG_TRACE) << "HTTP_MSG: " << pConnection;
            EventHttp(pConnection, nEvent, pData);
            break;
        case MG_EV_HTTP_CHUNK:  //partial message
            pmlLog(pml::LOG_TRACE) << "HTTP_CHUNK: " << pConnection;
            EventHttpChunk(pConnection, pData);
            break;
        case MG_EV_WRITE:
            EventWrite(pConnection);
            break;
        case MG_EV_CLOSE:
            pmlLog(pml::LOG_DEBUG) << "MongooseServer\tHandleClose";
            if (is_websocket(pConnection))
            {
                pConnection->fn_data = nullptr;
                CloseWebsocket(pConnection);
            }
            else
            {
                m_mFileDownloads.erase(pConnection);
                m_mConnectionQueue.erase(pConnection);
            }
            break;
        case MG_EV_POLL:
                HandleThreadedMessage(pConnection);
            break;
        default:
            break;
    }
}
void MongooseServer::HandleThreadedMessage(mg_connection* pConnection)
{
    auto itQueue = m_mConnectionQueue.find(pConnection);
    if(itQueue != m_mConnectionQueue.end())
    {
        response theResponse;
        if(itQueue->second.try_pop(theResponse))
        {
            DoReply(pConnection, theResponse);
        }
    }
}
void MongooseServer::HandleOpen(mg_connection* pConnection)
{
    if(m_sAcl.empty() == false && mg_check_ip_acl(mg_str(m_sAcl.c_str()), pConnection->rem.ip) != 1)
    {
        pmlLog(pml::LOG_DEBUG) << "Restgoose:Server\tHandleOpen: Not allowed due to ACL";
        pConnection->is_closing = 1;
    }
    else if(GetNumberOfConnections(m_mgr) > m_nMaxConnections)
    {
        pmlLog(pml::LOG_DEBUG) << "Restgoose:Server\tHandleOpen: REACHED MAXIMUM";
        pConnection->is_closing = 1;
    }
}

void MongooseServer::HandleAccept(mg_connection* pConnection)
{
    if(mg_url_is_ssl(m_sServerName.c_str()))
    {
        pmlLog(pml::LOG_DEBUG) << "Restgoose:Server\tAccept connection: Turn on TLS";
        struct mg_tls_opts tls_opts;
        if(m_Ca.empty())
        {
            tls_opts.ca = nullptr;
        }
        else
        {
            tls_opts.ca = m_Ca.string().c_str();
        }
        tls_opts.srvname.len = m_sHostname.length();
        tls_opts.srvname.ptr = m_sHostname.c_str();
        tls_opts.cert = m_Cert.string().c_str();
        tls_opts.certkey = m_Key.string().c_str();
        tls_opts.ciphers = "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256";
        mg_tls_init(pConnection, &tls_opts);
        if(pConnection->is_closing == 1)
        {
            pmlLog(pml::LOG_ERROR) << "Restgoose:Server\tCould not implement TLS";
        }
    }
}


MongooseServer::MongooseServer() :
    m_bLoop(true),
    m_mHeaders({{headerName("X-Frame-Options"), headerValue("sameorigin")},
                {headerName("Cache-Control"), headerValue("no-cache")},
                {headerName("X-Content-Type-Options"), headerValue("nosniff")},
                {headerName("Referrer-Policy"), headerValue("no-referrer")},
                {headerName("Server"), headerValue("unknown")},
                {headerName("Access-Control-Allow-Origin"), headerValue("*")},
                {headerName("Access-Control-Allow-Methods"), headerValue("GET, PUT, POST, HEAD, OPTIONS, DELETE")},
                {headerName("Access-Control-Allow-Headers"), headerValue("Content-Type, Accept, Authorization")},
                {headerName("Access-Control-Max-Age"), headerValue("3600")}})
{
    mg_log_set(2);
    mg_log_set_fn(mgpmlLog, NULL);
}

MongooseServer::~MongooseServer()
{
    Stop();

}

bool MongooseServer::Init(const std::filesystem::path& ca, const std::filesystem::path& cert, const std::filesystem::path& key, const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings)
{
    m_nPort = nPort;
    //check for ssl
    m_Key = key;
    m_Cert = cert;
    m_Ca = ca;

    if(m_Cert.empty() == false)
    {
        m_mHeaders.insert({headerName("Strict-Transport-Security"), headerValue("max-age=31536000; includeSubDomains")});
    }

    m_ApiRoot = apiRoot;

    char hostname[255];
    gethostname(hostname, 255);
    
    m_sHostname = hostname;

    std::stringstream ssRewrite;
    ssRewrite << "%80=https://" << m_sHostname;


    m_bWebsocket = bEnableWebsocket;
    m_bSendPings = bSendPings;

    pmlLog(pml::LOG_TRACE) << "Restgoose:Server\tWebsockets=" << m_bWebsocket;

    s_ServerOpts.root_dir = ".";

    mg_mgr_init(&m_mgr);

    SetInterface(addr, nPort);

    return true;
}

void MongooseServer::SetInterface(const ipAddress& addr, unsigned short nPort)
{
    std::stringstream ss;
    if(m_Cert.empty())
    {
        ss << "http://";
    }
    else
    {
        ss << "https://";
    }
    ss << addr << ":" << nPort;
    m_sServerName = ss.str();
}

void MongooseServer::Run(bool bThread, const std::chrono::milliseconds& timeout)
{
    m_bLoop = true;
    m_PollTimeout = timeout;

    if(bThread)
    {
        m_pThread = std::make_unique<std::thread>(&MongooseServer::Loop, this);
    }
    else
    {
        Loop();
    }

}

void MongooseServer::Loop()
{
    m_pConnection = mg_http_listen(&m_mgr, m_sServerName.c_str(), ev_handler, nullptr);

    if(m_pConnection)
    {
        m_pConnection->fn_data = reinterpret_cast<void*>(this);

        m_nPipe = mg_mkpipe(&m_mgr, pipe_handler, reinterpret_cast<void*>(this), true);
        if(m_nPipe == 0)
        {
            pmlLog(pml::LOG_TRACE) << "No pipe!";
        }
        pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tStarted: " << m_sServerName;

        auto now = std::chrono::high_resolution_clock::now();
        while (m_bLoop)
        {
            mg_mgr_poll(&m_mgr, m_PollTimeout.count());

            auto diff = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()-now.time_since_epoch()));
            if(m_loopCallback)
            {   //call the loopback functoin saying how long it is since we last called the loopback function
                m_loopCallback(diff);
            }

            if(m_bWebsocket && m_bSendPings)
            {
                SendAndCheckPings(diff);
            }
            now = std::chrono::high_resolution_clock::now();

            if(m_bWebsocket && m_nPipe == 0)
            {   //if we are doing websockets and for some reason our interupt pipe didn't get created then send the websocket messages
                SendWSQueue();
            }
        }
        mg_mgr_free(&m_mgr);
    }
    else
    {
        pmlLog(pml::LOG_ERROR) << "RestGoose:Server\tCould not start webserver";
    }

}

void MongooseServer::Stop()
{
    m_bLoop = false;
    if(m_pThread)
    {
        m_pThread->join();
        m_pThread = nullptr;
    }
}

bool MongooseServer::AddWebsocketEndpoint(const endpoint& theEndpoint, const std::function<bool(const endpoint&, const query&, const userName&, const ipAddress& )>& funcAuthentication, const std::function<bool(const endpoint&, const Json::Value&)>& funcMessage, const std::function<void(const endpoint&, const ipAddress&)>& funcClose)
{
    pml::LogStream lg;
    lg << "MongooseServer\t" << "AddWebsocketEndpoint <" << theEndpoint.Get() << "> ";

    if(!m_bWebsocket)
    {
        lg.SetLevel(pml::LOG_WARN);
        lg << "failed as websockets not enabled";
        return false;
    }


    return m_mWebsocketAuthenticationEndpoints.insert(std::make_pair(theEndpoint, funcAuthentication)).second &&
           m_mWebsocketMessageEndpoints.insert(std::make_pair(theEndpoint, funcMessage)).second &&
           m_mWebsocketCloseEndpoints.insert(std::make_pair(theEndpoint, funcClose)).second;
}

bool MongooseServer::AddEndpoint(const methodpoint& theMethodPoint, const std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName& )>& func, bool bUseThread)
{
    pml::LogStream lg;
    lg << "MongooseServer\t" << "AddEndpoint <" << theMethodPoint.first.Get() << ", " << theMethodPoint.second.Get() << "> ";
    if(m_mEndpoints.find(theMethodPoint) != m_mEndpoints.end())
    {
        lg(pml::LOG_TRACE) << "failed as methodpoint already exists";
        return false;
    }

    m_mEndpoints.try_emplace(theMethodPoint, std::make_pair(func, bUseThread));
    
    m_mmOptions.insert(std::make_pair(theMethodPoint.second, theMethodPoint.first));
    lg.SetLevel(pml::LOG_TRACE);
    lg << "success";
    return true;
}

bool MongooseServer::DeleteEndpoint(const methodpoint& theMethodPoint)
{
    m_mmOptions.erase(theMethodPoint.second);
    return (m_mEndpoints.erase(theMethodPoint) != 0);
}


void MongooseServer::SendError(mg_connection* pConnection, const std::string& sError, int nCode)
{
    DoReply(pConnection, response(nCode, sError));
}

void MongooseServer::DoReply(mg_connection* pConnection,const response& theResponse)
{
    if(theResponse.bFile == false)
    {
        DoReplyText(pConnection, theResponse);
    }
    else
    {
        DoReplyFile(pConnection, theResponse);
    }
}


std::string MongooseServer::CreateHeaders(const response& theResponse, size_t nLength)
{
    std::stringstream ssHeaders;
    ssHeaders << "HTTP/1.1 " << theResponse.nHttpCode << " \r\n";
    if(nLength > 0)
    {
        ssHeaders << "Content-Type: " << theResponse.contentType.Get() << "\r\n";
    }
    ssHeaders << "Content-Length: " << nLength << "\r\n";

    for(const auto& pairHeader : theResponse.mHeaders)
    {
        ssHeaders << pairHeader.first << ":" << pairHeader.second << "\r\n";
    }
    if(theResponse.mHeaders.empty())
    {
        for(const auto& pairHeader : m_mHeaders)
        {
            ssHeaders << pairHeader.first << ":" << pairHeader.second << "\r\n";
        }
    }

    for(const auto& pairHeader : theResponse.mExtraHeaders)
    {
        ssHeaders << pairHeader.first << ":" << pairHeader.second << "\r\n";
    }

    ssHeaders << "\r\n";
    return ssHeaders.str();
}

void MongooseServer::DoReplyText(mg_connection* pConnection, const response& theResponse)
{
    std::string sReply;
    if(theResponse.contentType.Get() == "application/json")
    {
        sReply = ConvertFromJson(theResponse.jsonData);
    }
    else
    {
        sReply = theResponse.data.Get();
    }

    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tDoReply " << theResponse.nHttpCode;
    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tDoReply " << sReply;

    std::stringstream ssHeaders;
    ssHeaders << "HTTP/1.1 " << theResponse.nHttpCode << " \r\n"
              << "Content-Type: " << theResponse.contentType.Get() << "\r\n"
              << "Content-Length: " << sReply.length() << "\r\n"
              << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
              << "Access-Control-Allow-Origin: *\r\n"
              << "Access-Control-Allow-Methods: GET, PUT, POST, HEAD, OPTIONS, DELETE\r\n"
              << "Access-Control-Allow-Headers: Content-Type, Accept, Authorization\r\n"
              << "Access-Control-Max-Age: 3600\r\n\r\n";


        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        mg_send(pConnection, sReply.c_str(), sReply.length());
}

void MongooseServer::DoReplyFile(mg_connection* pConnection, const response& theResponse)
{
    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tDoReplyFile " << theResponse.nHttpCode;
    pmlLog(pml::LOG_DEBUG) << "RestGoose:Server\tDoReplyFile " << theResponse.data;

    auto itIfs = m_mFileDownloads.insert(std::make_pair(pConnection, std::make_unique<std::ifstream>())).first;
    itIfs->second->open(theResponse.data.Get(), std::ifstream::ate | std::ifstream::binary);
    if(itIfs->second->is_open())
    {
        itIfs->second->seekg(0, itIfs->second->end);
        auto nLength = itIfs->second->tellg();
        itIfs->second->seekg(0, itIfs->second->beg);

        std::string sHeaders = CreateHeaders(theResponse, nLength);

        mg_send(pConnection, sHeaders.c_str(), sHeaders.length());
        EventWrite(pConnection);
        pConnection->is_resp = 0;
    }
    else
    {
        //send a 404 response
        m_mFileDownloads.erase(pConnection);
    }
}

void MongooseServer::EventWrite(mg_connection* pConnection)
{
    auto itFile = m_mFileDownloads.find(pConnection);
    if(itFile != m_mFileDownloads.end())
    {
        char buffer[65535];
        itFile->second->read(buffer, 65535);
        mg_send(pConnection, reinterpret_cast<void*>(buffer), itFile->second->gcount());
        if(itFile->second->gcount() < 65535)
        {
            m_mFileDownloads.erase(pConnection);
        }
    }
}


void MongooseServer::SendOptions(mg_connection* pConnection, const endpoint& theEndpoint)
{
    auto itOption = m_mmOptions.lower_bound(theEndpoint);
    if(itOption == m_mmOptions.upper_bound(theEndpoint))
    {
        std::stringstream ssHeaders;
        ssHeaders << "HTTP/1.1 404\r\n"
                << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
                  << "Access-Control-Allow-Origin: *\r\n"
                  << "Access-Control-Allow-Methods: OPTIONS";

        if(m_Cert.empty() == false)
        {
            ssHeaders << "Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n";
        }

        ssHeaders << "\r\n"
                  << "Content-Length: 0 \r\n"
                  << "Access-Control-Allow-Headers: Content-Type, Accept, Authorization\r\n"
                  << "Access-Control-Max-Age: 3600\r\n\r\n";

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        pConnection->is_resp = 0;

    }
    else
    {
        std::stringstream ssHeaders;
        ssHeaders << "HTTP/1.1 200\r\n"
                << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
                  << "Access-Control-Allow-Origin: *\r\n"
                  << "Access-Control-Allow-Methods: OPTIONS";
        if(m_Cert.empty() == false)
        {
            ssHeaders << "Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n";
        }

        for(; itOption != m_mmOptions.upper_bound(theEndpoint); ++itOption)
        {
            ssHeaders << ", " << itOption->second.Get();
        }
        ssHeaders << "\r\n"
                  << "Content-Length: 0 \r\n"
                  << "Access-Control-Allow-Headers: Content-Type, Accept, Authorization\r\n"
                  << "Access-Control-Max-Age: 3600\r\n\r\n";

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        pConnection->is_resp = 0;
        pConnection->is_draining = 1;
    }
}


void MongooseServer::SendAuthenticationRequest(mg_connection* pConnection)
{
     if(m_tokenCallback == nullptr)
    {
        std::stringstream ssHeaders;
        ssHeaders << "HTTP/1.1 401\r\n"
                << "WWW-Authenticate: Basic realm=\"User Visible Realm\"\r\n\r\n";

        if(m_Cert.empty() == false)
        {
            ssHeaders << "Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n";
        }

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        pConnection->is_draining = 1;
    }
    else if(m_tokenCallbackHandleNotAuthorized)
    {
        DoReply(pConnection, m_tokenCallbackHandleNotAuthorized());
    }
    else
    {
        //@todo ask application what to send back - possibly redirect to login page or something
        DoReply(pConnection, response(401, "Not authenticated"));
    }
}



void MongooseServer::SendWSQueue()
{
    std::lock_guard<std::mutex> lg(m_mutex);

    if(m_pConnection)
    {
        while(m_qWsMessages.empty() == false)
        {
            auto sMessage = ConvertFromJson(m_qWsMessages.front().second);

            pmlLog(pml::LOG_TRACE) << "SendWSQueue: " << sMessage;

            //turn message into array
            char *cstr = new char[sMessage.length() + 1];
            strcpy(cstr, sMessage.c_str());



            for (mg_connection* pConnection = m_pConnection->mgr->conns; pConnection != NULL; pConnection = pConnection->next)
            {
                if(is_websocket(pConnection))
                {
                    auto itSubscriber = m_mSubscribers.find(pConnection);
                    if(itSubscriber != m_mSubscribers.end())
                    {
                        if(itSubscriber->second.bAuthenticated) //authenticated
                        {
                            for(auto anEndpoint : m_qWsMessages.front().first)
                            {
                                if(WebsocketSubscribedToEndpoint(itSubscriber->second, anEndpoint))
                                {
                                    mg_ws_send(pConnection, cstr, strlen(cstr), WEBSOCKET_OP_TEXT);
                                    break;
                                }
                            }
                        }
                        else
                        {
                            pmlLog(pml::LOG_TRACE) << "RestGoose:Server\t" << itSubscriber->second.theEndpoint << " not yet authenticated...";
                        }
                    }
                }
            }
            delete[] cstr;
            m_qWsMessages.pop();
        }
    }
}

bool MongooseServer::WebsocketSubscribedToEndpoint(const subscriber& sub, const endpoint& anEndpoint)
{
    auto vEndpoint = SplitString(anEndpoint.Get() , '/');
    for(auto endp : sub.setEndpoints)
    {
        auto vSub = SplitString(endp.Get(), '/');
        if(vSub.size() <= vEndpoint.size())
        {
            if(vSub == std::vector<std::string>(vEndpoint.begin(), vEndpoint.begin()+vSub.size()))
            {
                return true;
            }
        }
    }
    return false;
}


void MongooseServer::SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage)
{
    pmlLog(pml::LOG_TRACE) << "RestGoose:Server\tSendWebsocketMessage";
    m_mutex.lock();
    m_qWsMessages.push(wsMessage(setEndpoints, jsMessage));
    m_mutex.unlock();
    if(m_nPipe != 0)
    {
        const char* hi="hi";
        auto res = send(m_nPipe, hi, 2, 0);
    }
}

void MongooseServer::SetLoopCallback(const std::function<void(std::chrono::milliseconds)>& func)
{
    std::lock_guard<std::mutex> lg(m_mutex);
    m_loopCallback = func;
}


bool MongooseServer::AddBAUser(const userName& aUser, const password& aPassword)
{
    if(m_tokenCallback) return false;

    std::lock_guard<std::mutex> lg(m_mutex);

    if(auto ins = m_mUsers.insert(std::make_pair(aUser, aPassword)); ins.second == false)
    {
        ins.first->second = aPassword;
        return true;
    }
    return false;
}

bool MongooseServer::DeleteBAUser(const userName& aUser)
{
    if(m_tokenCallback) return false;

    std::lock_guard<std::mutex> lg(m_mutex);
    m_mUsers.erase(aUser);
    return true;
}

std::set<methodpoint> MongooseServer::GetEndpoints() const
{
    std::set<methodpoint> setEndpoints;
    for(const auto& [thePoint, fn] : m_mEndpoints)
    {
        setEndpoints.insert(thePoint);
    }
    return setEndpoints;
}


bool MongooseServer::InApiTree(const endpoint& theEndpoint)
{
    return (theEndpoint.Get().length() >= m_ApiRoot.Get().length() && theEndpoint.Get().substr(0, m_ApiRoot.Get().length()) == m_ApiRoot.Get());
}


void MongooseServer::PrimeWait()
{
    std::scoped_lock lock(m_mutex);
    m_signal.nHttpCode = 0;
}

void MongooseServer::Wait()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    while(m_signal.nHttpCode == 0)
    {
        m_cvSync.wait(lk);
    }
}

const response& MongooseServer::GetSignalResponse() const
{
    return m_signal;
}

void MongooseServer::Signal(const response& resp)
{
    std::scoped_lock lock(m_mutex);
    m_signal = resp;
    m_cvSync.notify_one();
}


void MongooseServer::AddNotFoundCallback(const std::function<response(const httpMethod&, const query&, const std::vector<partData>&, const endpoint&, const userName&)>& func)
{
    m_callbackNotFound = func;
}

void MongooseServer::SetAuthorizationTypeBearer(const std::function<bool(const methodpoint&, const std::string&)>& callback, const std::function<response()>& callbackHandleNotAuthorized, bool bAuthenticateWebsocketsViaQuery)
{
    if(callback)
    {
        m_mUsers.clear();
        m_tokenCallback = callback;
        m_tokenCallbackHandleNotAuthorized = callbackHandleNotAuthorized;
        m_bAuthenticateWSViaQuery = bAuthenticateWebsocketsViaQuery;
    }
}

void MongooseServer::SetAuthorizationTypeBasic(const userName& aUser, const password& aPassword)
{
    m_tokenCallback = nullptr;
    AddBAUser(aUser, aPassword);
}

void MongooseServer::SetAuthorizationTypeNone()
{
    m_tokenCallback = nullptr;
    m_mUsers.clear();
}


void MongooseServer::SetMaxConnections(size_t nMax)
{
    m_nMaxConnections = nMax;
}

void MongooseServer::SendAndCheckPings(const std::chrono::milliseconds& elapsed)
{
    m_timeSinceLastPingSent += elapsed;
    if(m_timeSinceLastPingSent > std::chrono::milliseconds(1000))
    {
        m_timeSinceLastPingSent = std::chrono::milliseconds(0);
        if(m_pConnection)
        {
            for (mg_connection* pConnection = m_pConnection->mgr->conns; pConnection != NULL; pConnection = pConnection->next)
            {
                if(is_websocket(pConnection))
                {
                    auto itSub = m_mSubscribers.find(pConnection);
                    if(itSub != m_mSubscribers.end())
                    {
                            //check PONG timeout
                        if(!itSub->second.bPonged)    //not replied within the last second
                        {
                            pmlLog(pml::LOG_WARN) << "Websocket from " << itSub->second.peer << " has not responded to PING. Close";
                            pConnection->is_closing = true;
                            //call the close callback
                            auto itCallback = m_mWebsocketCloseEndpoints.find(itSub->second.theEndpoint);
                            if(itCallback != m_mWebsocketCloseEndpoints.end())
                            {
                                itCallback->second(itSub->second.theEndpoint, itSub->second.peer);
                            }
                        }
                        else
                        {
                            mg_ws_send(pConnection, "hi", 2, WEBSOCKET_OP_PING);
                            itSub->second.bPonged = false;
                        }
                    }
                }
            }
        }
    }
}

size_t MongooseServer::GetNumberOfWebsocketConnections() const
{
    return DoGetNumberOfWebsocketConnections(m_mgr);
}

void MongooseServer::SetUnprotectedEndpoints(const std::set<methodpoint>& setUnprotected)
{
    m_setUnprotected = setUnprotected;
}

bool MongooseServer::MethodPointUnprotected(const methodpoint& thePoint)
{
    if(m_setUnprotected.find(thePoint) != m_setUnprotected.end())
    {
        return true;
    }

    auto vEndpoint = SplitString(thePoint.second.Get() , '/');

    for(auto sub : m_setUnprotected)
    {
        if(sub.first == thePoint.first)
        {
            auto vSub = SplitString(sub.second.Get(), '/');

            if(vSub.empty() == false && vSub.back() == "*" && vSub.size() <= vEndpoint.size())
            {
                bool bOk = true;
                for(size_t i = 0; i < vSub.size()-1; i++)
                {
                    if(vSub[i] != vEndpoint[i])
                    {
                        bOk = false;
                        break;
                    }
                }
                if(bOk)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void MongooseServer::AddHeaders(const std::map<headerName, headerValue>& mHeaders)
{
    for(const auto& pairHeader : mHeaders)
    {
        m_mHeaders[pairHeader.first] = pairHeader.second;
    }
}

void MongooseServer::RemoveHeaders(const std::set<headerName>& setHeaders)
{
    for(const auto& header : setHeaders)
    {
        m_mHeaders.erase(header);
    }
}

void MongooseServer::SetHeaders(const std::map<headerName, headerValue>& mHeaders)
{
    m_mHeaders = mHeaders;
}
