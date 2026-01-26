#include "mongooseserver.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>

#include "json/json.h"
#include "log.h"

#include "threadpool.h"
#include "utils.h"


bool case_ins_less(std::string_view s1, std::string_view s2)
{
    return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), [](unsigned char a, unsigned char b){ return toupper(a) < toupper(b); });
}




static void mgpml_log(char ch, void*)
{
    static pml::log::Stream ls;
    ls.SetLevel(pml::log::Level::kDebug);
    if(ch != '\n')
    {
        ls << ch;
    }
    else
    {
        ls << std::endl;
    }
}



bool operator<(const methodpoint& e1, const methodpoint& e2)
{
    return (e1.first.Get() < e2.first.Get() || (e1.first.Get() == e2.first.Get() && case_ins_less(e1.second.Get(), e2.second.Get())));
}



using namespace std::placeholders;
namespace pml::restgoose
{

const std::string kDisposition = "Content-Disposition: ";
const std::string kName = " name=";
const std::string kFilename = " filename=";

const std::string kLogPrefix = "pml::restgoose";

static struct mg_http_serve_opts s_ServerOpts;

bool end_less::operator() (endpoint e1, endpoint e2) const
{
    return std::lexicographical_compare(e1.Get().begin(), e1.Get().end(), e2.Get().begin(), e2.Get().end(), [](unsigned char a, unsigned char b){
                                        return toupper(a) < toupper(b);
                                        });
}



size_t get_number_of_connections(const mg_mgr& mgr)
{
    size_t nCount = 0;
    if(mg_connection* pTmp = mgr.conns; pTmp)
    {
        nCount++;
        while((pTmp = pTmp->next) != nullptr)
        {
            ++nCount;
        }
    }
    return nCount;
}

size_t do_get_number_of_websocket_connections(const mg_mgr& mgr)
{
    size_t nCount = 0;
    if(mg_connection* pTmp = mgr.conns; pTmp)
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

headerValue get_header(struct mg_http_message* pMessage, const headerName& name)
{
    headerValue value("");
    if(mg_str const* pStr = mg_http_get_header(pMessage, name.Get().c_str()); pStr && pStr->len > 0)
    {
        value = headerValue(std::string(pStr->buf, pStr->len));
    }
    return value;
}



std::vector<partData> create_part_data(const mg_str& str, const headerValue& contentType)
{
    pml::log::trace("pml::restgoose") << "CreatePartData " << contentType;
    if(contentType.Get() != "application/x-www-form-urlencoded")
    {
        return {partData(partName(""), textData(std::string(str.buf, str.buf+str.len)))};
    }
    else
    {
        auto vSplit = SplitString(std::string(str.buf, str.buf+str.len), '&');
        std::vector<partData> vData;
        vData.reserve(vSplit.size());

        for(const auto& sEntry : vSplit)
        {
            auto nPos = sEntry.find('=');
            if(nPos == std::string::npos)
            {
                vData.emplace_back(partName(""), textData(sEntry));
            }
            else
            {
                vData.emplace_back(partName(sEntry.substr(0,nPos)), textData(sEntry.substr(nPos+1)));
            }
        }
        return vData;
    }
}

partData create_part_data(const mg_http_part& mgpart, const headerValue& )
{
    partData part(partName(std::string(mgpart.name.buf, mgpart.name.len)), textData(std::string(mgpart.filename.buf, mgpart.filename.len)), CreateTmpFileName("/tmp"));

    if(part.filepath.empty() == false)
    {
        std::ofstream ofs;
        ofs.open(part.filepath.string());
        if(ofs.is_open())
        {
            ofs.write(mgpart.body.buf, mgpart.body.len);
            ofs.close();
        }
    }
    else
    {
        part.data = textData(std::string(mgpart.body.buf, mgpart.body.buf+mgpart.body.len));
    }
    return part;
}


std::string decode_query_string(mg_http_message const* pMessage)
{
    if(pMessage->query.len > 0)
    {
        char decode[6000];
        mg_url_decode(pMessage->query.buf, pMessage->query.len, decode, 6000, 0);
        return std::string(decode);
    }
    return std::string("");
}

query extract_query(mg_http_message const* pMessage)
{
    query mDecode;

    auto sQuery = decode_query_string(pMessage);

    auto vQuery = SplitString(sQuery, '&');
    for(const auto& sParam : vQuery)
    {
        auto vValue = SplitString(sParam, '=');
        if(vValue.size() == 2)
        {
            mDecode.try_emplace(queryKey(vValue[0]), vValue[1]);
        }
        else if(vValue.size() == 1)
        {
            mDecode.try_emplace(queryKey(vValue[0]), "");
        }
    }
    return mDecode;
}


static int is_websocket(const struct mg_connection* nc)
{
    return nc->is_websocket;
}



void ev_handler(mg_connection *pConnection, int nEvent, void* pData)
{
    if(nEvent == 0 || pConnection->fn_data == nullptr)
    {
        return;
    }

    auto pThread = reinterpret_cast<MongooseServer*>(pConnection->fn_data);

    pThread->HandleEvent(pConnection, nEvent, pData);
}


void MongooseServer::EventWebsocketOpen(mg_connection const*, int , void* ) const
{
    pml::log::trace("pml::restgoose") << "EventWebsocketOpen";
}

bool MongooseServer::AuthenticateWebsocket(const subscriber& sub, const Json::Value& jsData)
{
    std::scoped_lock lg(m_mutex);

    pml::log::debug("pml::restgoose") << "AuthenticateWebsocket";
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
                    pml::log::info("pml::restgoose") << "Websocket subscriber: " << sub.peer << " authorized";

                    return true;
                }
                else
                {
                    pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " not authorized";
                    return false;
                }
            }
            else
            {
                pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " endpoint: " << sub.theEndpoint << " has not authorization function";
                return false;
            }
        }
        else
        {
            pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " User not found or password not correct";
            return false;
        }
    }
    else
    {
        pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " No user or password sent";
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
        pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " No bearer token sent";
        return false;
    }
    pml::log::debug("pml::restgoose") << "AuthenticateWebsocketBearer token=" << sToken;

    if(auto itEndpoint = m_mWebsocketAuthenticationEndpoints.find(sub.theEndpoint); itEndpoint != m_mWebsocketAuthenticationEndpoints.end())
    {
        if(itEndpoint->second(sub.theEndpoint, sub.queryParams, userName(sToken), sub.peer))
        {
            pml::log::info("pml::restgoose") << "Websocket subscriber: " << sub.peer << " authorized";
            return true;
        }
        else
        {
            pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " not authorized";
            return false;
        }
    }
    else
    {
        pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " endpoint: " << sub.theEndpoint << " has not authorization function";
        return false;
    }


}



void MongooseServer::EventWebsocketMessage(mg_connection* pConnection, int, void* pData )
{
    pml::log::trace("pml::restgoose") << "RestGoose::Server\tEventWebsocketMessage";

    if(auto itSubscriber = m_mSubscribers.find(pConnection); itSubscriber != m_mSubscribers.end())
    {
        auto pMessage = reinterpret_cast<mg_ws_message*>(pData);
        std::string sMessage(pMessage->data.buf, pMessage->data.len);

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
            pml::log::error("pml::restgoose") << "Unable to convert '" << sMessage << "' to JSON: " << e.what();
        }
    }

}

void MongooseServer::DoWebsocketAuthentication(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData)
{
    if(sub.bAuthenticated = AuthenticateWebsocket(sub, jsData); sub.bAuthenticated)
    {
        pml::log::trace("pml::restgoose") << "RestGoose::Server\tHandleInternalWebsocketMessage: Authenticated";
        AddWebsocketSubscriptions(sub, jsData);
    }
    else
    {
        pml::log::log(pml::log::Level::kDebug) << "tWebsocket subscriber not authenticated: close";
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
            pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " attempted to send data before authenticating. " << " Close the connection";
            m_mSubscribers.erase(pConnection);
            pConnection->is_closing = 1;
        }
    }

}

void MongooseServer::HandleExternalWebsocketMessage(mg_connection* pConnection, const subscriber& sub, const Json::Value& jsData)
{
    pml::log::trace("pml::restgoose") << "RestGoose::Server\tHandleExternalWebsocketMessage";
    if(sub.bAuthenticated)
    {
        if(auto itEndpoint = m_mWebsocketMessageEndpoints.find(sub.theEndpoint); itEndpoint != m_mWebsocketMessageEndpoints.end())
        {
            itEndpoint->second(sub.theEndpoint, jsData);
        }
        else
        {
            pml::log::warning("pml::restgoose") << "" << sub.peer << " has no message methodpoint!";
        }
    }
    else
    {
        pml::log::warning("pml::restgoose") << "Websocket subscriber: " << sub.peer << " attempted to send data before authenticating. " <<  " Close the connection";
        m_mSubscribers.erase(pConnection);
        pConnection->is_closing = 1;
    }
}

void MongooseServer::AddWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData) const
{
    pml::log::debug("pml::restgoose") << "tWebsocket subscriber: " << sub.peer << " adding subscriptions " << jsData;

    if(jsData["endpoints"].isArray())
    {
        for(const auto& jsEndpoint : jsData["endpoints"])
        {
            pml::log::debug("pml::restgoose") << "tWebsocket subscriber: " << sub.peer << " adding subscription " << jsEndpoint.asString();
            sub.setEndpoints.emplace(jsEndpoint.asString());
        }
    }
    else
    {
        pml::log::debug("pml::restgoose") << "tWebsocket subscriber: Incorrect JSON";
    }
}

void MongooseServer::RemoveWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData) const
{
    pml::log::debug("pml::restgoose") << "Websocket subscriber: " << sub.peer << " removing subscriptions " << jsData;

    if(jsData["endpoints"].isArray())
    {
        for(const auto& jsEndpoint : jsData["endpoints"])
        {
            sub.setEndpoints.erase(endpoint(jsEndpoint.asString()));
        }
    }
}

void MongooseServer::EventWebsocketCtl(mg_connection *pConnection, int , void* pData)
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
                sData.assign(pMessage->data.buf, pMessage->data.len);

                pml::log::debug("pml::restgoose") << "Websocket ctl: [" << (int)pMessage->flags << "] " << sData;
            }
            break;
        case WEBSOCKET_OP_CLOSE:
            {
                pml::log::debug("pml::restgoose") << "WebsocketCtl - close";
                CloseWebsocket(pConnection);
            }
            break;
    }
}

void MongooseServer::CloseWebsocket(mg_connection* pConnection)
{
    if(auto itSub = m_mSubscribers.find(pConnection); itSub != m_mSubscribers.end())
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
    if(auto thePoint = GetMethodPoint(pMessage); MethodPointUnprotected(thePoint))
    {
        return std::make_pair(true, userName(""));
    }
    else if(m_tokenCallback)
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
    std::scoped_lock lg(m_mutex);

    char sUser[255];
    char sPass[255];
    mg_http_creds(pMessage, sUser, 255, sPass, 255);

    if(auto itUser = m_mUsers.find(userName(sUser)); itUser != m_mUsers.end() && itUser->second.Get() == std::string(sPass))
    {
        return std::make_pair(true, itUser->first);
    }
    else
    {
        pml::log::info("pml::restgoose") << "CheckAuthorization: user '" << sUser <<" with given password not found";
        return std::make_pair(false, userName(""));
    }
}

authorised MongooseServer::CheckAuthorizationBearer(const methodpoint& thePoint, mg_http_message* pMessage)
{
    std::scoped_lock lg(m_mutex);

    char sBearer[65535];
    char sPass[65535];
    mg_http_creds(pMessage, sBearer, 65535, sPass, 65535);
    
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
    mg_url_decode(pMessage->uri.buf, pMessage->uri.len, decode, 6000, 0);

    std::string sUri(decode);

    if(sUri[sUri.length()-1] == '/')    //get rid of trailling /
    {
        sUri = sUri.substr(0, sUri.length()-1);
    }

    //@todo possibly allow user to decide if should be case insensitive or not
    //transform(sUri.begin(), sUri.end(), sUri.begin(), ::tolower);

    //remove any double /
    std::string sPath;
    char c(0);
    for(const auto& ch : sUri)
    {
        if(ch != c || c != '/')
        {
            sPath += ch;
        }
        c = ch;
    }
    sUri = sPath;

    std::string sMethod(pMessage->method.buf);
    size_t nSpace = sMethod.find(' ');
    sMethod = sMethod.substr(0, nSpace);
    pml::log::debug("pml::restgoose") << "GetMethodPoint: " << sMethod << "\t" << sUri;

    return methodpoint(httpMethod(sMethod), endpoint(sUri));

}

void MongooseServer::EventHttp(mg_connection *pConnection, int, void* pData)
{
    auto pMessage = reinterpret_cast<mg_http_message*>(pData);

    auto thePoint = GetMethodPoint(pMessage);
    auto content = mg_http_get_header(pMessage, "Content-Type");


    std::string sContents;
    if(content && content->len > 0)
    {
        sContents = std::string(content->buf, content->len);
    }

    if(CmpNoCase(thePoint.first.Get(), "OPTIONS"))
    {
        SendOptions(pConnection, thePoint.second);
    }
    else if(InApiTree(thePoint.second))
    {

        pml::log::debug("pml::restgoose") << "'" << thePoint.second << "' in Api tree";
        if(auto itWsEndpoint = m_mWebsocketAuthenticationEndpoints.find(thePoint.second); itWsEndpoint != m_mWebsocketAuthenticationEndpoints.end())
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
        pml::log::debug("pml::restgoose") << "'" << thePoint.second << "' not in Api tree";
        auto [bAuth, user] = CheckAuthorization(pMessage);
        if(bAuth == false)
        {
            SendAuthenticationRequest(pConnection, thePoint, false);
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
    pml::log::trace("pml::restgoose") << "RestGoose::Server\tEventHttpWebsocket";

    mg_ws_upgrade(pConnection, pMessage, nullptr);

    //create the peer address
    char buffer[256];
    mg_snprintf(buffer, sizeof(buffer), "%M", mg_print_ip, &pConnection->rem);

    std::stringstream ssPeer;
    ssPeer << buffer << ":" << pConnection->rem.port;

    auto itSub = m_mSubscribers.try_emplace(pConnection, uri, ipAddress(ssPeer.str()), extract_query(pMessage)).first;
    itSub->second.setEndpoints.insert(uri);

    if(m_mUsers.empty() && m_tokenCallback == nullptr)
    {   //if we have not set up any users then we are not using authentication so we don't need to authenticate the websocket
        itSub->second.bAuthenticated = true;
        pml::log::trace("pml::restgoose") << "RestGoose::Server\tEventHttpWebsocket: Authenticated";
    }
    else if(m_tokenCallback && m_bAuthenticateWSViaQuery)
    {   //we are using bearer token and passing it to the websocket via the query string
        DoWebsocketAuthentication(pConnection, itSub->second, Json::Value(Json::nullValue));
    }

}

const ipAddress& MongooseServer::GetCurrentPeer(bool bIncludePort) const
{
    return bIncludePort ? m_lastPeerAndPort : m_lastPeer;

}

void MongooseServer::EventHttpApi(mg_connection *pConnection, mg_http_message* pMessage, const methodpoint& thePoint)
{
    auto [bAuth, user] = CheckAuthorization(pMessage);
    if(bAuth == false)
    {
        SendAuthenticationRequest(pConnection, thePoint, true);
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
        if(auto itCallback = m_mEndpoints.find(thePoint); itCallback != m_mEndpoints.end())
        {
            if(itCallback->second.second == false)
            {
                DoReply(pConnection, itCallback->second.first(extract_query(pMessage), create_part_data(pMessage->body, get_header(pMessage, headerName("Content-Type"))), thePoint.second, user));
            }
            else
            {
                DoReplyThreaded(pConnection, extract_query(pMessage), create_part_data(pMessage->body, get_header(pMessage, headerName("Content-Type"))), thePoint, user);
            }
        }
        else if(m_callbackNotFound)
        {
            DoReply(pConnection, m_callbackNotFound(thePoint.first, extract_query(pMessage), {}, thePoint.second, user));
        }
        else
        {
            SendError(pConnection, "Not Found", 404);
        }
    }
}

void MongooseServer::DoReplyThreaded(mg_connection* pConnection,const query& theQuery, const std::vector<partData>& theData, const methodpoint& thePoint, const userName& theUser)
{
    m_mConnectionQueue.try_emplace(pConnection /* emplacing a default constructed object */);
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

            mg_wakeup(&m_mgr, pConnection->id, nullptr, 0 /* No data */);
            
            
        }
    });
}

void MongooseServer::EventHttpApiMultipart(mg_connection *pConnection, mg_http_message* pMessage, const methodpoint& thePoint)
{
    auto [bAuth, user]= CheckAuthorization(pMessage);
    if(bAuth == false)
    {
        SendAuthenticationRequest(pConnection, thePoint, true);
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
                vData.push_back(create_part_data(part, get_header(pMessage, headerName("Content-Type"))));
            }
            
            if(itCallback->second.second == false)
            {
                DoReply(pConnection, itCallback->second.first(extract_query(pMessage), vData, thePoint.second, user));
            }
            else
            {
                DoReplyThreaded(pConnection, extract_query(pMessage), vData, thePoint, user);
            }
        }
        else if(m_callbackNotFound)
        {
            DoReply(pConnection, m_callbackNotFound(thePoint.first, extract_query(pMessage), {}, thePoint.second, user));
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
                pml::log::error("pml::restgoose") << reinterpret_cast<char*>(pData);
            }
            break;
        case MG_EV_OPEN:
            pml::log::debug("pml::restgoose") << "HandleOpen";
            HandleOpen(pConnection);
            break;
        case MG_EV_ACCEPT:
            pml::log::debug("pml::restgoose") << "HandleAccept";
            HandleAccept(pConnection);
            break;
        case MG_EV_WS_OPEN:
            pml::log::debug("pml::restgoose") << "HandleWebsocketOpen";
            EventWebsocketOpen(pConnection, nEvent, pData);
            break;
        case MG_EV_WS_CTL:
            pml::log::trace("pml::restgoose") << "HandleWebsocketCtl";
            EventWebsocketCtl(pConnection, nEvent, pData);
            break;
        case MG_EV_WS_MSG:
            pml::log::debug("pml::restgoose") << "HandleWebsocketMsg";
            EventWebsocketMessage(pConnection, nEvent, pData);
            break;
        case MG_EV_HTTP_MSG:
            pml::log::debug("pml::restgoose") << "HandleHTTPMsg";
            pml::log::trace("pml::restgoose") << "HTTP_MSG: " << pConnection;
            EventHttp(pConnection, nEvent, pData);
            break;
        case MG_EV_WRITE:
            EventWrite(pConnection);
            break;
        case MG_EV_CLOSE:
            pml::log::debug("pml::restgoose") << "HandleClose";
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
        case MG_EV_WAKEUP:    //this gets called each loop
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
    if(pConnection)
    {
        if(m_sAcl.empty() == false && mg_check_ip_acl(mg_str(m_sAcl.c_str()), &pConnection->rem) != 1)
        {
            pml::log::debug("pml::restgoose") << "HandleOpen: Not allowed due to ACL";
            pConnection->is_closing = 1;
        }
        else if(get_number_of_connections(m_mgr) > m_nMaxConnections)
        {
            pml::log::debug("pml::restgoose") << "HandleOpen: REACHED MAXIMUM";
            pConnection->is_closing = 1;
        }
    }
}

void MongooseServer::HandleAccept(mg_connection* pConnection) const
{
    if(mg_url_is_ssl(m_sServerName.c_str()))
    {
        pml::log::debug("pml::restgoose") << "Accept connection: Turn on TLS";
        struct mg_tls_opts tls_opts;
        if(m_sCa.empty())
        {
            pml::log::debug("pml::restgoose") << "TLS No CA";
            tls_opts.ca.buf = nullptr;
            tls_opts.ca.len = 0;

        }
        else
        {
            pml::log::debug("pml::restgoose") << "TLS We have a CA";
            tls_opts.ca = mg_str(m_sCa.c_str());
        }
        pml::log::debug("pml::restgoose") << "TLS Hostname: " << m_sHostname;

        tls_opts.name = mg_str(m_sHostname.c_str());
        
        tls_opts.skip_verification = 0;
        tls_opts.cert = mg_str(m_sCert.c_str());
        tls_opts.key = mg_str(m_sKey.c_str());

        
        mg_tls_init(pConnection, &tls_opts);
        if(pConnection->is_closing == 1)
        {
            pml::log::error("pml::restgoose") << "Could not implement TLS";
        }
    }
}


MongooseServer::MongooseServer()
{
  
    mg_log_set(2);  //info and worse
    //mg_log_set_fn(mgpml_log, nullptr);
}

MongooseServer::~MongooseServer()
{
    Stop();

}

bool MongooseServer::Init(const std::filesystem::path& ca, const std::filesystem::path& cert, const std::filesystem::path& key, const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings)
{
    m_sKey = load_tls(key);
    m_sCert = load_tls(cert);
    m_sCa = load_tls(ca);
    

    if(m_sCert.empty() == false)
    {
       m_mHeaders.try_emplace(headerName("Strict-Transport-Security"), headerValue("max-age=31536010; includeSubDomains"));
    }

    m_ApiRoot = apiRoot;

    char hostname[255];
    gethostname(hostname, 255);
    
    m_sHostname = hostname;

    std::stringstream ssRewrite;
    ssRewrite << "%80=https://" << m_sHostname;


    m_bWebsocket = bEnableWebsocket;
    m_bSendPings = bSendPings;

    pml::log::trace("pml::restgoose") << "Websockets=" << m_bWebsocket;

    s_ServerOpts.root_dir = ".";

    mg_mgr_init(&m_mgr);
    
    mg_wakeup_init(&m_mgr); //init multithreaded wakeup

    SetInterface(addr, nPort);

    return true;
}

void MongooseServer::SetInterface(const ipAddress& addr, unsigned short nPort)
{
    m_sServerName = (m_sCert.empty() ? "http://" : "https://");
    m_sServerName += addr.Get();
    m_sServerName += (nPort == 0 ? "" : ":"+std::to_string(nPort));

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

        pml::log::debug("pml::restgoose") << "Started: " << m_sServerName;

        auto now = std::chrono::high_resolution_clock::now();
        while (m_bLoop)
        {
            mg_mgr_poll(&m_mgr, static_cast<int>(m_PollTimeout.count()));

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

            if(m_bWebsocket)
            {   //We are now sending ws in the loop rather than using the pipe as it keeps breaking
                SendWSQueue();
            }
        }
        mg_mgr_free(&m_mgr);
    }
    else
    {
        pml::log::error("pml::restgoose") << "Could not start webserver";
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
    pml::log::Stream lg(pml::log::Level::kInfo, kLogPrefix);
    lg << "AddWebsocketEndpoint <" << theEndpoint.Get() << "> ";

    if(!m_bWebsocket)
    {
        lg.SetLevel(pml::log::Level::kWarning);
        lg << "failed as websockets not enabled";
        return false;
    }


    return m_mWebsocketAuthenticationEndpoints.try_emplace(theEndpoint, funcAuthentication).second &&
           m_mWebsocketMessageEndpoints.try_emplace(theEndpoint, funcMessage).second &&
           m_mWebsocketCloseEndpoints.try_emplace(theEndpoint, funcClose).second;
}

bool MongooseServer::AddEndpoint(const methodpoint& theMethodPoint, const std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName& )>& func, bool bUseThread)
{
    pml::log::Stream lg(pml::log::Level::kInfo, kLogPrefix);
    lg << "AddEndpoint <" << theMethodPoint.first.Get() << ", " << theMethodPoint.second.Get() << "> ";
    if(m_mEndpoints.find(theMethodPoint) != m_mEndpoints.end())
    {
        lg(pml::log::Level::kTrace) << "failed as methodpoint already exists";
        return false;
    }

    m_mEndpoints.try_emplace(theMethodPoint, func, bUseThread);
    
    m_mmOptions.insert(std::make_pair(theMethodPoint.second, theMethodPoint.first));
    lg.SetLevel(pml::log::Level::kDebug);
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
    DoReply(pConnection, response(static_cast<unsigned short>(nCode), sError));
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


std::string MongooseServer::CreateHeaders(const response& theResponse, size_t nLength) const
{
    std::stringstream ssHeaders;
    ssHeaders << "HTTP/1.1 " << theResponse.nHttpCode << " \r\n";
    if(nLength > 0)
    {
        ssHeaders << "Content-Type: " << theResponse.contentType.Get() << "\r\n";
    }
    ssHeaders << "Content-Length: " << nLength << "\r\n";

    for(const auto& [name, value]  : theResponse.mHeaders)
    {
        ssHeaders << name << ":" << value << "\r\n";
    }
    if(theResponse.mHeaders.empty())
    {
        for(const auto& [name, value] : m_mHeaders)
        {
            ssHeaders << name << ":" << value << "\r\n";
        }
    }

    for(const auto& [name, value] : theResponse.mExtraHeaders)
    {
        ssHeaders << name << ":" << value << "\r\n";
    }

    ssHeaders << "\r\n";
    return ssHeaders.str();
}

void MongooseServer::DoReplyText(mg_connection* pConnection, const response& theResponse) const
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

    pml::log::debug("pml::restgoose") << "DoReply " << theResponse.nHttpCode;
    pml::log::debug("pml::restgoose") << "DoReply " << sReply;

    std::string sHeaders = CreateHeaders(theResponse, sReply.length());

    mg_send(pConnection, sHeaders.c_str(), sHeaders.length());
    mg_send(pConnection, sReply.c_str(), sReply.length());
    pConnection->is_draining = 1;
}

void MongooseServer::DoReplyFile(mg_connection* pConnection, const response& theResponse)
{
    pml::log::debug("pml::restgoose") << "DoReplyFile " << theResponse.nHttpCode;
    pml::log::debug("pml::restgoose") << "DoReplyFile " << theResponse.data;

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
        pConnection->is_draining = 1;

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

        if(m_sCert.empty() == false)
        {
            ssHeaders << "\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains";
        }

        ssHeaders << "\r\nContent-Length: 0 \r\n"
                  << "Access-Control-Allow-Headers: Content-Type, Accept, Authorization\r\n"
                  << "Access-Control-Max-Age: 3600\r\n\r\n";

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        pConnection->is_draining = 1;

    }
    else
    {
        std::stringstream ssHeaders;
        ssHeaders << "HTTP/1.1 200\r\n"
                << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
                  << "Access-Control-Allow-Origin: *\r\n"
                  << "Access-Control-Allow-Methods: OPTIONS";
        for(; itOption != m_mmOptions.upper_bound(theEndpoint); ++itOption)
        {
            ssHeaders << ", " << itOption->second.Get();
        }
	if(m_sCert.empty() == false)
        {
            ssHeaders << "\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains";
        }

        ssHeaders << "\r\nContent-Length: 0 \r\n"
                  << "Access-Control-Allow-Headers: Content-Type, Accept, Authorization\r\n"
                  << "Access-Control-Max-Age: 3600\r\n\r\n";

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        pConnection->is_resp = 0;
        pConnection->is_draining = 1;
    }
}


void MongooseServer::SendAuthenticationRequest(mg_connection* pConnection, const methodpoint& thePoint, bool bApi)
{
     if(m_tokenCallback == nullptr)
    {
        std::stringstream ssHeaders;
        ssHeaders << "HTTP/1.1 401\r\n"
                << "WWW-Authenticate: Basic realm=\"User Visible Realm\"\r\n";

        if(m_sCert.empty() == false)
        {
            ssHeaders << "Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n";
        }
	    ssHeaders << "\r\n";

        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        pConnection->is_draining = 1;
    }
    else if(m_tokenCallbackHandleNotAuthorized)
    {   //let the application decide what to send back
        DoReply(pConnection, m_tokenCallbackHandleNotAuthorized(thePoint.second, bApi));
    }
    else
    {
        DoReply(pConnection, response(401, std::string("Not authenticated")));
    }
}



void MongooseServer::SendWSQueue()
{
    //std::scoped_lock lg(m_mutex);

    if(m_pConnection)
    {
        wsMessage message;
        
        while(m_qWsMessages.try_dequeue(message))
        {
            auto sMessage = ConvertFromJson(message.second);

            pml::log::trace("pml::restgoose") << "SendWSQueue: " << sMessage;

            //turn message into array - for some reason sending sMessage.c_str() does not work
            auto cstr = new char[sMessage.length() + 1];
            strcpy(cstr, sMessage.c_str());


            for (auto pConnection = m_pConnection->mgr->conns; pConnection != nullptr; pConnection = pConnection->next)
            {
                if(is_websocket(pConnection))
                {
                    auto itSubscriber = m_mSubscribers.find(pConnection);
                    if(itSubscriber != m_mSubscribers.end())
                    {
                        if(itSubscriber->second.bAuthenticated) //authenticated
                        {
                            for(const auto& anEndpoint : message.first)
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
                            pml::log::trace("pml::restgoose") << "" << itSubscriber->second.theEndpoint << " not yet authenticated...";
                        }
                    }
                }
            }
            delete[] cstr;
        }
    }
}

bool MongooseServer::WebsocketSubscribedToEndpoint(const subscriber& sub, const endpoint& anEndpoint) const
{
    
    auto vEndpoint = SplitString(anEndpoint.Get() , '/');
    for(auto endp : sub.setEndpoints)
    {
        pml::log::trace("pml::restgoose") << "WebsocketSubscribedToEndpoint: " << endp;
        auto vSub = SplitString(endp.Get(), '/');
        if(vSub.size() <= vEndpoint.size() && vSub == std::vector<std::string>(vEndpoint.begin(), vEndpoint.begin()+vSub.size()))
        {
            return true;
        }
    }
    return false;
}


void MongooseServer::SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage)
{
    m_qWsMessages.try_enqueue({setEndpoints, jsMessage});
}

void MongooseServer::SetLoopCallback(const std::function<void(std::chrono::milliseconds)>& func)
{
    std::scoped_lock lg(m_mutex);
    m_loopCallback = func;
}


bool MongooseServer::AddBAUser(const userName& aUser, const password& aPassword)
{
    if(m_tokenCallback) return false;

    std::scoped_lock lg(m_mutex);

    if(auto [it, bIns] = m_mUsers.try_emplace(aUser, aPassword); bIns == false)
    {
        it->second = aPassword;
        return true;
    }
    return false;
}

bool MongooseServer::DeleteBAUser(const userName& aUser)
{
    if(m_tokenCallback) return false;

    std::scoped_lock lg(m_mutex);
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
    std::unique_lock lk(m_mutex);
    m_cvSync.wait(lk, [this](){return m_signal.nHttpCode != 0;});
    
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

void MongooseServer::SetAuthorizationTypeBearer(const std::function<bool(const methodpoint&, const std::string&)>& callback, const std::function<response(const endpoint&, bool)>& callbackHandleNotAuthorized, bool bAuthenticateWebsocketsViaQuery)
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
            for (mg_connection* pConnection = m_pConnection->mgr->conns; pConnection != nullptr; pConnection = pConnection->next)
            {
                if(is_websocket(pConnection))
                {
                    auto itSub = m_mSubscribers.find(pConnection);
                    if(itSub != m_mSubscribers.end())
                    {
                            //check PONG timeout
                        if(!itSub->second.bPonged)    //not replied within the last second
                        {
                            pml::log::warning("pml::restgoose") << "Websocket from " << itSub->second.peer << " has not responded to PING. Close";
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
    return do_get_number_of_websocket_connections(m_mgr);
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

    for(const auto& [method, endp] : m_setUnprotected)
    {
        if(method == thePoint.first)
        {
            auto vSub = SplitString(endp.Get(), '/');

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
    for(const auto& [name, value] : mHeaders)
    {
        m_mHeaders[name] = value;
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

void MongooseServer::SetStaticDirectory(std::string_view sDir)
{
    pml::log::info("pml::restgoose") << "MongooseServer::SetStaticDirectory " << sDir;
     m_sStaticRootDir = sDir;
}

}
