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

using namespace std;
using namespace std::placeholders;


const std::string DISPOSITION = "Content-Disposition: ";
const std::string NAME = " name=";
const std::string FILENAME = " filename=";

static struct mg_http_serve_opts s_ServerOpts;

std::string CreateTmpFileName()
{
    std::stringstream sstr;
    auto tp = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
    sstr << seconds.count();
    sstr << "_" << (std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count()%1000000000);
    return sstr.str();
}




partData CreatePartData(const mg_str& str)
{
    partData part;
    part.sData.assign(str.ptr, str.ptr+str.len);
    return part;
}

partData CreatePartData(const mg_http_part& mgpart)
{
    partData part;
    part.sName.assign(mgpart.name.ptr, mgpart.name.len);
    part.sFilename.assign(mgpart.filename.ptr, mgpart.filename.len);
    part.sData = "/tmp/"+CreateTmpFileName();
    if(part.sFilename.empty() == false)
    {
        std::ofstream ofs;
        ofs.open(part.sData);
        if(ofs.is_open())
        {
            ofs.write(mgpart.body.ptr, mgpart.body.len);
            ofs.close();
        }
    }
    else
    {
        part.sData.assign(mgpart.body.ptr, mgpart.body.ptr+mgpart.body.len);
    }
    return part;
}


bool RG_EXPORT operator<(const methodpoint& e1, const methodpoint& e2)
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
        pmlLog(pml::LOG_DEBUG) << "Mongoose: " << str;
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



void ev_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data)
{
    if(nEvent == 0)
    {
        return;
    }

    MongooseServer* pThread = reinterpret_cast<MongooseServer*>(pConnection->fn_data);
    pThread->HandleEvent(pConnection, nEvent, pData);
}

void pipe_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data)
{
    if(nEvent == MG_EV_READ)
    {
        MongooseServer* pThread = reinterpret_cast<MongooseServer*>(pConnection->fn_data);
        pThread->SendWSQueue();
    }
}


void MongooseServer::EventWebsocketOpen(mg_connection *pConnection, int nEvent, void* pData)
{

    //mg_ws_message* pMessage = reinterpret_cast<mg_ws_message*>(pData);

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
            auto itEndpoint = m_mWebsocketAuthenticationEndpoints.find(sub.theEndpoint);
            if(itEndpoint != m_mWebsocketAuthenticationEndpoints.end())
            {
                if(itEndpoint->second(sub.theEndpoint, itUser->first, sub.peer))
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
                pmlLog(pml::LOG_WARN) << "Websocket subscriber: " << sub.peer << " endpoint: " << sub.theEndpoint << " has not authorization function";
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
            pmlLog() << "Websocket subscriber not authenticated: close";
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
        auto itEndpoint = m_mWebsocketMessageEndpoints.find(sub.theEndpoint);
        if(itEndpoint != m_mWebsocketMessageEndpoints.end())
        {
            itEndpoint->second(sub.theEndpoint, jsData);
        }
        else
        {
            pmlLog(pml::LOG_WARN) << sub.peer << " has no message methodpoint!";
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
    pmlLog(pml::LOG_DEBUG) << "Websocket subscriber: " << sub.peer << " adding subscriptions " << jsData;

    if(jsData["endpoints"].isArray())
    {
        for(Json::ArrayIndex ai = 0; ai < jsData["endpoints"].size(); ++ai)
        {
            sub.setEndpoints.insert(endpoint(jsData["endpoints"][ai].asString()));
        }
    }
}

void MongooseServer::RemoveWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData)
{
    pmlLog(pml::LOG_DEBUG) << "Websocket subscriber: " << sub.peer << " removing subscriptions " << jsData;

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
            auto itEndpoint = m_mWebsocketCloseEndpoints.find(itSub->second.theEndpoint);
            if(itEndpoint != m_mWebsocketCloseEndpoints.end())
            {
                itEndpoint->second(itSub->second.theEndpoint, itSub->second.peer);
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
        pmlLog(pml::LOG_INFO) << "CheckAuthorization: user '" << sUser <<" with given password not found";
        return std::make_pair(false, userName(""));
    }

}

methodpoint MongooseServer::GetMethodPoint(mg_http_message* pMessage)
{
    char decode[6000];
    mg_url_decode(pMessage->uri.ptr, pMessage->uri.len, decode, 6000, 0);

    string sUri(decode);
    if(sUri[sUri.length()-1] == '/')    //get rid of trailling /
    {
        sUri = sUri.substr(0, sUri.length()-1);
    }

    transform(sUri.begin(), sUri.end(), sUri.begin(), ::tolower);
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

    string sMethod(pMessage->method.ptr);
    size_t nSpace = sMethod.find(' ');
    sMethod = sMethod.substr(0, nSpace);
    return methodpoint(httpMethod(sMethod), endpoint(sUri));

}

void MongooseServer::HandleFirstChunk(httpchunks& chunk, mg_connection* pConnection, mg_http_message* pMessage)
{
    auto auth = CheckAuthorization(pMessage);
    if(auth.first == false)
    {
        SendAuthenticationRequest(pConnection);
    }
    else
    {
        if(pMessage->query.len > 0)
        {
            char decode[6000];
            mg_url_decode(pMessage->query.ptr, pMessage->query.len, decode, 6000, 0);
            chunk.theQuery = query(std::string(decode));
        }
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
        if(contents->len > 0)
        {
            chunk.sContentType.assign(contents->ptr, contents->len);
        }

        if(chunk.sContentType.find("multipart") != std::string::npos)
        {
            WorkoutBoundary(chunk);
        }
        else
        {
            chunk.vParts.push_back(partData());
            chunk.vParts.back().sFilename = CreateTmpFileName();
            chunk.vParts.back().sData = "/tmp/"+chunk.vParts.back().sFilename;
            chunk.ofs.open(chunk.vParts.back().sData);
            if(chunk.ofs.is_open() == false)
            {
                pmlLog(pml::LOG_WARN) << "MongooseServer\tCould not create temp file '" << chunk.vParts.back().sFilename << "' for upload";
            }
        }

        auto totalSize = mg_http_get_header(pMessage, "Content-Length");
        if(totalSize->len > 0)
        {
            try
            {
                chunk.nTotalSize = std::stoul(std::string(totalSize->ptr, totalSize->len));
            }
            catch(const std::exception& e)
            {
                pmlLog(pml::LOG_WARN) << "MongooseServer\tCould not decode message length";
            }
        }
        pmlLog(pml::LOG_DEBUG) << "MongooseServer\tFirst chunk: " << chunk.sContentType << "\t" << chunk.nTotalSize << " bytes";
    }
}

void MongooseServer::WorkoutBoundary(httpchunks& chunk)
{
    auto vSplit = SplitString(chunk.sContentType, ';');
    if(vSplit.size() > 1)
    {
        if(vSplit[1].find("boundary") != std::string::npos)
        {
            chunk.sBoundary = "\r\n--"+vSplit[1].substr(10)+"\r\n";
            chunk.sBoundaryLast = "\r\n--"+vSplit[1].substr(10)+"--\r\n";
            //chunk.sBoundaryLast = chunk.sBoundary+"--\r\n";
            chunk.vBuffer.reserve(chunk.sBoundaryLast.length());
            chunk.vBuffer.push_back('\r');
            chunk.vBuffer.push_back('\n');
        }
    }
}

void MongooseServer::EventHttpChunk(mg_connection *pConnection, void* pData)
{
    mg_http_message* pMessage = reinterpret_cast<mg_http_message*>(pData);

    auto ins  = m_mChunks.insert({pConnection, httpchunks()});
    if(ins.second)
    {
        HandleFirstChunk(ins.first->second, pConnection, pMessage);
    }

    //if the content type is multipart then we try to extract
    if(ins.first->second.sContentType.find("multipart") != std::string::npos)
    {
        HandleMultipartChunk(ins.first->second, pMessage);
    }
    else
    {
        HandleGenericChunk(ins.first->second, pMessage);
    }

    ins.first->second.nCurrentSize += pMessage->chunk.len;
    if(ins.first->second.nCurrentSize >= ins.first->second.nTotalSize)
    {   //received all the data
        HandleLastChunk(ins.first->second);
    }

    mg_http_delete_chunk(pConnection, pMessage);
}

void MongooseServer::HandleGenericChunk(httpchunks& chunk, mg_http_message* pMessage)
{
    if(chunk.ofs.is_open())
    {
        chunk.ofs.write(pMessage->chunk.ptr, pMessage->chunk.len);
        chunk.ofs.flush();
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

    if(chunk.ofs.is_open())
    {
        chunk.ofs.flush();    //write to disk

    }
}

void MongooseServer::HandleLastChunk(httpchunks& chunk)
{
    pmlLog(pml::LOG_DEBUG) << "MongooseServer\tAll chunks received. Now do something with them...";
    chunk.vBuffer.clear();
    if(chunk.ofs.is_open())
    {
        chunk.ofs.close();
    }
    if(chunk.pCallback)
    {
        chunk.pCallback(chunk.theQuery, chunk.vParts, chunk.thePoint.second, chunk.theUser);
    }
    else
    {
        pmlLog(pml::LOG_ERROR) << "MongooseServer\tSomeone uploaded a big file to a non allowed endpoint";
        //@todo in the end we shouldn't get here.
        //for now remove any files that were uploaded
        for(auto data : chunk.vParts)
        {
            if(data.sData.empty() == false)
            {
                remove(data.sData.c_str());
            }
        }
    }

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
    pmlLog(pml::LOG_DEBUG) << "Boundary found! " << chunk.nCurrentSize;

    if(chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().sFilename.empty() == false && chunk.ofs.is_open())
        {
            chunk.ofs.close();
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
    pmlLog(pml::LOG_DEBUG) << "Last Boundary found! " << chunk.nCurrentSize;

    if(chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().sFilename.empty() == false && chunk.ofs.is_open())
        {
            chunk.ofs.close();
        }
    }
    chunk.vBuffer.clear();
}

void MongooseServer::MultipartChunkBoundarySearch(httpchunks& chunk, char c)
{
    //store the buffered data before clearing it
    if(chunk.vBuffer.empty() == false && chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().sFilename.empty())
        {
            chunk.vParts.back().sData.append(chunk.vBuffer.begin(), chunk.vBuffer.end());
        }
        else if(chunk.ofs.is_open())
        {
            chunk.ofs.write(chunk.vBuffer.data(), chunk.vBuffer.size());
        }
    }
    chunk.vBuffer.clear();

    if(c == chunk.sBoundaryLast[0])
    {   //start the matching process again
        chunk.vBuffer.push_back(c);
    }
    else if(chunk.vParts.empty() == false)
    {
        if(chunk.vParts.back().sFilename.empty())
        {
            chunk.vParts.back().sData += c;
        }
        else if(chunk.ofs.is_open())
        {
            chunk.ofs << c;
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

                        chunk.vParts.back().sName =  sPart.substr(nStart, nEnd-nStart);

                        pmlLog() << "Chunk: Name='" << chunk.vParts.back().sName << "'";
                    }
                    else if(sPart.length() > FILENAME.length() && sPart.substr(0, FILENAME.length()) == FILENAME)
                    {
                        auto nStart = sPart.find('"')+1;
                        auto nEnd = sPart.find('"', nStart);

                        chunk.vParts.back().sFilename =  sPart.substr(nStart, nEnd-nStart);
                        chunk.vParts.back().sData = "/tmp/"+CreateTmpFileName();

                        chunk.ofs.open(chunk.vParts.back().sData);
                        if(chunk.ofs.is_open() == false)
                        {
                            pmlLog(pml::LOG_WARN) << "MongooseServer\tMultipart upload - Could not open file '" << chunk.vParts.back().sData << "'";
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
    mg_http_message* pMessage = reinterpret_cast<mg_http_message*>(pData);

    auto thePoint = GetMethodPoint(pMessage);
    auto content = mg_http_get_header(pMessage, "Content-Type");

    pmlLog(pml::LOG_DEBUG) << "MongooseServer\tEndpoint: <" << thePoint.first << ", " << thePoint.second << ">";

    std::string sContents;
    if(content && content->len > 0)
    {
        sContents = std::string(content->ptr, content->len);
        pmlLog(pml::LOG_DEBUG) << "MongooseServer\tContent: " << sContents;
    }

    if(CmpNoCase(thePoint.first.Get(), "OPTIONS"))
    {
        SendOptions(pConnection, thePoint.second);
    }
    else if(InApiTree(thePoint.second))
    {
        pmlLog(pml::LOG_DEBUG) << "API call";
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
        pmlLog(pml::LOG_DEBUG) << "Non-API call";
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
    pmlLog(pml::LOG_DEBUG) << "Websocket subscription";

    mg_ws_upgrade(pConnection, pMessage, nullptr);
    char buffer[256];
    mg_ntoa(&pConnection->peer, buffer, 256);
    std::stringstream ssPeer;
    ssPeer << buffer << ":" << pConnection->peer.port;
    auto itSub = m_mSubscribers.insert(std::make_pair(pConnection, subscriber(uri, ipAddress(ssPeer.str())))).first;
    if(m_mUsers.empty())
    {   //if we have not set up any users then we are not using authentication so we don't need to authenticate the websocket
        itSub->second.bAuthenticated = true;
    }
    itSub->second.setEndpoints.insert(uri);
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
        std::string sQuery;

        if(pMessage->query.len > 0)
        {
            char decode[6000];
            mg_url_decode(pMessage->query.ptr, pMessage->query.len, decode, 6000, 0);
            sQuery = std::string(decode);
        }

        //find the callback function assigned to the method and endpoint
        auto itCallback = m_mEndpoints.find(thePoint);
        if(itCallback != m_mEndpoints.end())
        {
            DoReply(pConnection, itCallback->second(query(sQuery), {CreatePartData(pMessage->body)}, thePoint.second, auth.second));
        }
        else if(m_callbackNotFound)
        {
            DoReply(pConnection, m_callbackNotFound(query(sQuery), {}, thePoint.second, auth.second));
        }
        else
        {
            SendError(pConnection, "Not Found", 404);
        }
    }
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
        std::string sQuery;
        if(pMessage->query.len > 0)
        {
            char decode[6000];
            mg_url_decode(pMessage->query.ptr, pMessage->query.len, decode, 6000, 0);
            sQuery = std::string(decode);
        }

        //find the callback function assigned to the method and endpoint
        auto itCallback = m_mEndpoints.find(thePoint);
        if(itCallback != m_mEndpoints.end())
        {
            postData theData;
            struct mg_http_part part;
            size_t nOffset=0;
            while((nOffset = mg_http_next_multipart(pMessage->body, nOffset, &part)) > 0)
            {
                theData.push_back(CreatePartData(part));
            }
            DoReply(pConnection, itCallback->second(query(sQuery), theData, thePoint.second, auth.second));
        }
        else if(m_callbackNotFound)
        {
            DoReply(pConnection, m_callbackNotFound(query(sQuery), {}, thePoint.second, auth.second));
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
        case MG_EV_HTTP_CHUNK:  //partial message
            EventHttpChunk(pConnection, pData);
            break;
        case MG_EV_CLOSE:
            pmlLog(pml::LOG_TRACE) << "MG_EV_CLOSE";
            if (is_websocket(pConnection))
            {
                pmlLog(pml::LOG_TRACE) << "MongooseServer\tWebsocket closed";
                pConnection->fn_data = nullptr;
                m_mSubscribers.erase(pConnection);
            }
            pmlLog(pml::LOG_TRACE) << "MongooseServer\tDone";
            break;
        case 0:
        case MG_EV_POLL:
        case MG_EV_READ:
        case MG_EV_WRITE:
            break;
        default:
            pmlLog(pml::LOG_INFO) << "EVENT: " << nEvent;
    }
}

void MongooseServer::HandleAccept(mg_connection* pConnection)
{
    pmlLog(pml::LOG_TRACE) << "MongooseServer::HandleAccept";

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
        mg_tls_init(pConnection, &tls_opts);
        if(pConnection->is_closing == 1)
        {
            pmlLog(pml::LOG_ERROR) << "Could not implement TLS";
        }
    }
}


MongooseServer::MongooseServer() :
    m_pConnection(nullptr),
    m_pPipe(nullptr),
    m_bWebsocket(false),
    m_nPort(0),
    m_nPollTimeout(100),
    m_loopCallback(nullptr),
    m_bLoop(true),
    m_pThread(nullptr),
    m_callbackNotFound(nullptr)
{
    #ifdef __WXDEBUG__
    mg_log_set("2");
    mg_log_set_callback(mgpmlLog, NULL);
    #endif // __WXDEBUG__


}

MongooseServer::~MongooseServer()
{
    Stop();

}

bool MongooseServer::Init(const std::string& sCert, const std::string& sKey, int nPort, const std::string& sApiRoot, bool bEnableWebsocket)
{
    m_nPort = nPort;
    //check for ssl
    m_sKey = sKey;
    m_sCert = sCert;

    m_sApiRoot = sApiRoot;

    char hostname[255];
    gethostname(hostname, 255);
    stringstream ssRewrite;
    ssRewrite << "%80=https://" << hostname;

    m_bWebsocket = bEnableWebsocket;


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

    return true;
}

void MongooseServer::Run(bool bThread, unsigned int nTimeoutMs)
{
    m_bLoop = true;
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

void MongooseServer::Loop()
{
    m_pConnection = mg_http_listen(&m_mgr, m_sServerName.c_str(), ev_handler, nullptr);

    if(m_pConnection)
    {
        m_pConnection->fn_data = reinterpret_cast<void*>(this);

        m_pPipe = mg_mkpipe(&m_mgr, pipe_handler, nullptr);
        if(m_pPipe)
        {
            m_pPipe->fn_data = reinterpret_cast<void*>(this);
        }

        pmlLog(pml::LOG_INFO) << "Server started: " << m_sServerName;
        pmlLog(pml::LOG_INFO) << "--------------------------";

        auto now = std::chrono::high_resolution_clock::now();
        while (m_bLoop)
        {
            mg_mgr_poll(&m_mgr, m_nPollTimeout);

            if(m_loopCallback)
            {   //call the loopback functoin saying how long it is since we last called the loopback function
                m_loopCallback((std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()-now.time_since_epoch())).count());
                now = std::chrono::high_resolution_clock::now();
            }

//            if(m_bWebsocket && !m_pPipe)
//            {   //if we are doing websockets and for some reason our interupt pipe didn't get created then send the websocket messages
//                SendWSQueue();
//            }
        }
        mg_mgr_free(&m_mgr);
    }
    else
    {
        pmlLog(pml::LOG_ERROR) << "Could not start webserver";
    }

}

void MongooseServer::Stop()
{
    if(m_pThread)
    {
        m_bLoop = false;
        m_pThread->join();
        m_pThread = nullptr;
    }
}

bool MongooseServer::AddWebsocketEndpoint(const endpoint& theEndpoint, std::function<bool(const endpoint&, const userName&, const ipAddress& )> funcAuthentication, std::function<bool(const endpoint&, const Json::Value&)> funcMessage, std::function<void(const endpoint&, const ipAddress&)> funcClose)
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

bool MongooseServer::AddEndpoint(const methodpoint& theMethodPoint, std::function<response(const query&, const postData&, const endpoint&, const userName& )> func)
{
    pml::LogStream lg;
    lg << "MongooseServer\t" << "AddEndpoint <" << theMethodPoint.first.Get() << ", " << theMethodPoint.second.Get() << "> ";
    if(m_mEndpoints.find(theMethodPoint) != m_mEndpoints.end())
    {
        lg.SetLevel(pml::LOG_WARN);
        lg << "failed as methodpoint already exists";
        return false;
    }

    m_mEndpoints.insert(std::make_pair(theMethodPoint, func));
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


void MongooseServer::SendError(mg_connection* pConnection, const string& sError, int nCode)
{
    DoReply(pConnection, response(nCode, sError));
}

void MongooseServer::DoReply(mg_connection* pConnection,const response& theResponse)
{
    std::string sReply;
    if(theResponse.sContentType == "application/json")
    {
        sReply = ConvertFromJson(theResponse.jsonData);
    }
    else
    {
        sReply = theResponse.sData;
    }

    pmlLog(pml::LOG_DEBUG) << "MongooseServer::DoReply " << theResponse.nHttpCode;
    pmlLog(pml::LOG_DEBUG) << "MongooseServer::DoReply " << sReply;



    stringstream ssHeaders;
    ssHeaders << "HTTP/1.1 " << theResponse.nHttpCode << " \r\n"
              << "Content-Type: " << theResponse.sContentType << "\r\n"
              << "Content-Length: " << sReply.length() << "\r\n"
              << "X-Frame-Options: sameorigin\r\nCache-Control: no-cache\r\nStrict-Transport-Security: max-age=31536000; includeSubDomains\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: unknown\r\n"
              << "Access-Control-Allow-Origin:*\r\n"
              << "Access-Control-Allow-Methods:GET, PUT, POST, HEAD, OPTIONS, DELETE\r\n"
              << "Access-Control-Allow-Headers:Content-Type, Accept, Authorization\r\n"
              << "Access-Control-Max-AgeL3600\r\n\r\n";


        mg_send(pConnection, ssHeaders.str().c_str(), ssHeaders.str().length());
        mg_send(pConnection, sReply.c_str(), sReply.length());
}


void MongooseServer::SendOptions(mg_connection* pConnection, const endpoint& theEndpoint)
{
    auto itOption = m_mmOptions.lower_bound(theEndpoint);
    if(itOption == m_mmOptions.upper_bound(theEndpoint))
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

        for(; itOption != m_mmOptions.upper_bound(theEndpoint); ++itOption)
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
                        pmlLog(pml::LOG_TRACE) << "Send Websocket messsage: subscriber: '" << itSubscriber->second.theEndpoint << "'";

                        if(itSubscriber->second.bAuthenticated) //authenticated
                        {
                            pmlLog(pml::LOG_TRACE) << itSubscriber->second.theEndpoint << "authenticated...";

                            bool bSent(false);
                            for(auto anEndpoint : m_qWsMessages.front().first)
                            {
                                for(auto sub : itSubscriber->second.setEndpoints)
                                {
                                    pmlLog(pml::LOG_TRACE) << "Subscriber: " << sub;
                                    if(sub.Get().length() <= anEndpoint.Get().length() && anEndpoint.Get().substr(0, sub.Get().length()) == sub.Get())
                                    {   //has subscribed to something upstream of this methodpoint
                                        pmlLog(pml::LOG_TRACE) << "Send websocket message from: " << anEndpoint;
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
                            pmlLog(pml::LOG_TRACE) << itSubscriber->second.theEndpoint << " not yet authenticated...";
                        }
                    }
                }
            }
            delete[] cstr;
            m_qWsMessages.pop();
        }
    }
}


void MongooseServer::SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage)
{
    m_mutex.lock();
    m_qWsMessages.push(wsMessage(setEndpoints, jsMessage));
    m_mutex.unlock();
    if(m_pPipe)
    {
        mg_mgr_wakeup(m_pPipe);
    }
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

std::set<methodpoint> MongooseServer::GetEndpoints()
{
    std::set<methodpoint> setEndpoints;
    for(auto pairEnd : m_mEndpoints)
    {
        setEndpoints.insert(pairEnd.first);
    }
    return setEndpoints;
}


bool MongooseServer::InApiTree(const endpoint& theEndpoint)
{
    return (theEndpoint.Get().length() >= m_sApiRoot.length() && theEndpoint.Get().substr(0, m_sApiRoot.length()) == m_sApiRoot);
}


void MongooseServer::PrimeWait()
{
    lock_guard<mutex> lock(m_mutex);
    m_eOk = WAIT;
}

void MongooseServer::Wait()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    while(m_eOk == WAIT)
    {
        m_cvSync.wait(lk);
    }
}

const std::string& MongooseServer::GetSignalData()
{
    return m_sSignalData;
}

void MongooseServer::Signal(bool bOk, const std::string& sData)
{
    lock_guard<mutex> lock(m_mutex);
    if(bOk)
    {
        m_eOk = SUCCESS;
    }
    else
    {
        m_eOk = FAIL;
    }
    m_sSignalData = sData;
    m_cvSync.notify_one();
}

bool MongooseServer::IsOk()
{
    lock_guard<mutex> lock(m_mutex);
    return (m_eOk == SUCCESS);
}


void MongooseServer::AddNotFoundCallback(std::function<response(const query&, const postData&, const endpoint&, const userName&)> func)
{
    m_callbackNotFound = func;
}



