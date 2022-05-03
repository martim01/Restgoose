#pragma once

extern "C" {
#include "mongoose.h"
}
#include <fstream>
#include <string>
#include <sstream>
#include <mutex>
#include <map>
#include <set>
#include <queue>
#include <vector>
#include <initializer_list>
#include "response.h"
#include <functional>
#include "namedtype.h"
#include <list>
#include <atomic>
#include <thread>
#include <condition_variable>

extern RG_EXPORT bool operator<(const methodpoint& e1, const methodpoint& e2);
extern size_t GetNumberOfConnections(mg_mgr& mgr);

using wsMessage = std::pair<std::set<endpoint>, Json::Value>;
using authorised = std::pair<bool, userName>;



namespace pml
{
    namespace restgoose
    {
        class Server;
        class MongooseServer
        {
            public:
                //friend void pipe_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data);
               // friend void ev_handler(mg_connection *pConnection, int nEvent, void* pData, void* fn_data);

                friend class Server;

                bool Init(const fileLocation& cert, const fileLocation& key, const ipAddress& addr,  int nPort, const endpoint& apiRoot, bool bEnableWebsocket);

                void SetInterface(const ipAddress& addr, unsigned short nPort);

                void SetAuthorizationTypeBearer(std::function<bool(const std::string&)> callback);
                void SetAuthorizationTypeBasic(const userName& aUser, const password& aPassword);
                void SetAuthorizationTypeNone();

                bool AddBAUser(const userName& aUser, const password& aPassword);
                bool DeleteBAUser(const userName& aUser);


                void SetMaxConnections(size_t nMax);

                /** @brief Creates the thread that runs the webserver loop
                *   @param bThread if true will run in a separate thread, if false will run in main thread
                *   @param nTimeoutms the time in milliseconds to wait for a mongoose event to happen
                **/
                void Run(bool bThread, const std::chrono::milliseconds& timeout);


                ///< @brief Stops the server
                void Stop();


                bool AddWebsocketEndpoint(const endpoint& theEndpoint, std::function<bool(const endpoint&, const userName&, const ipAddress&)> funcAuthentication,
                                          std::function<bool(const endpoint&, const Json::Value&)> funcMessage,
                                          std::function<void(const endpoint&, const ipAddress&)> funcClose);

                /** Adds a callback handler for an methodpoint
                *   @param theMethodPoint a pair definining the HTTP method and methodpoint address
                *   @param func std::function that defines the callback function
                *   @return <i>bool</i> true on success
                **/
                bool AddEndpoint(const methodpoint& theMethodPoint, std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)> func);

                /** @brief Adds a callback handler that is called if no handler is found for the endpoint
                **/
                void AddNotFoundCallback(std::function<response(const httpMethod&, const query&, const std::vector<partData>&, const endpoint&, const userName&)> func);

                /** Removes a callback handler for an methodpoint
                *   @param theMethodPoint a pair definining the HTTP method and methodpoint address
                *   @return <i>bool</i> true on success
                **/
                bool DeleteEndpoint(const methodpoint& theMethodPoint);

                /** Sets the function that will be called every time the poll function times out or an event happens
                *   @param func the function to call. It will be passed one argument, the number of milliseconds since it was last called
                **/
                void SetLoopCallback(std::function<void(std::chrono::milliseconds)> func);

                void SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage);



                std::set<methodpoint> GetEndpoints();


                void SetStaticDirectory(const std::string& sDir) { m_sStaticRootDir = sDir;}
                const std::string& GetStaticDirectory() const {return m_sStaticRootDir;}

                unsigned long GetPort() const { return m_nPort; }


                void Wait();
                void PrimeWait();
                bool IsOk();
                void Signal(const response& resp);
                const response& GetSignalResponse() const;


                /** Handles an event
                *   @param pConnection the mg_connection that caused the event
                *   @param nEvent the event type
                *   @param pData any associated data
                **/
                void HandleEvent(mg_connection *pConnection, int nEvent, void* pData);

                void SendWSQueue();

                ~MongooseServer();

            protected:

                MongooseServer();


            private:


                ///< @brief the main mongoose loop. Called in a separate thread by Run()
                void Loop();

                /** @brief Handles all Mongoose websocket events
                *   @param pConnection the mg_connection that caused the event
                *   @param nEvent the event type
                *   @param pData any associated data
                **/
                void EventWebsocketOpen(mg_connection *pConnection, int nEvent, void* pData);

                void EventWebsocketMessage(mg_connection *pConnection, int nEvent, void* pData);
                void EventWebsocketCtl(mg_connection *pConnection, int nEvent, void* pData);


                /** @brief Handles all Mongoose http request events
                *   @param pConnection the mg_connection that caused the event
                *   @param nEvent the event type
                *   @param pData any associated data
                **/
                void EventHttp(mg_connection *pConnection, int nEvent, void* pData);
                void EventWrite(mg_connection* pConnection);
                void EventHttpWebsocket(mg_connection *pConnection, mg_http_message* pMessage, const endpoint& uri);
                void EventHttpApi(mg_connection *pConnection, mg_http_message* pMessage, const methodpoint& thePoint);
                void EventHttpApiMultipart(mg_connection *pConnection, mg_http_message* pMessage, const methodpoint& thePoint);

                /** @brief Send a JSON encoded error message to the provided connection containing the provided error
                *   @param pConnection the mg_connection to send the data to
                *   @param sError the error message
                *   @param nCode the error code
                **/
                void SendError(mg_connection* pConnection, const std::string& sError, int nCode=-1);


         //       bool MultipartBegin(mg_connection* pConnection, http_message* pMessage);
         //       bool PartBegin(mg_connection* pConnection, mg_http_multipart_part* pPart);
        //        bool PartData(mg_connection* pConnection, mg_http_multipart_part* pPart);
        //        bool PartEnd(mg_connection* pConnection, mg_http_multipart_part* pPart);
        //        bool MultipartEnd(mg_connection* pConnection, mg_http_multipart_part* pPart);




                void DoReply(mg_connection* pConnection, const response& theResponse);
                void DoReplyText(mg_connection* pConnection, const response& theResponse);
                void DoReplyFile(mg_connection* pConnection, const response& theResponse);
                void SendAuthenticationRequest(mg_connection* pConnection);

                void SendOptions(mg_connection* pConnection, const endpoint& thEndpoint);


                void ClearMultipartData();

                authorised CheckAuthorization(struct mg_http_message* pMessage);
                authorised CheckAuthorizationBasic(struct mg_http_message* pMessage);
                authorised CheckAuthorizationBearer(struct mg_http_message* pMessage);

                struct subscriber
                {
                    subscriber(const endpoint& anEndpoint, const ipAddress& Ip) : theEndpoint(anEndpoint), peer(Ip), bAuthenticated(false){}
                    endpoint theEndpoint;
                    ipAddress peer;
                    bool bAuthenticated;
                    std::set<endpoint> setEndpoints;
                };

                void HandleInternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData);
                void HandleExternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData);

                void AddWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData);
                void RemoveWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData);

                bool AuthenticateWebsocket(subscriber& sub, const Json::Value& jsData);
                bool AuthenticateWebsocketBasic(subscriber& sub, const Json::Value& jsData);
                bool AuthenticateWebsocketBearer(subscriber& sub, const Json::Value& jsData);

                void HandleAccept(mg_connection* pConnection);

                void EventHttpChunk(mg_connection *pConnection, void* pData);

                methodpoint GetMethodPoint(mg_http_message* pMessage);

                bool InApiTree(const endpoint& theEndpoint);

                mg_connection* m_pConnection;
                int m_nPipe;
                std::string m_sIniPath;
                std::string m_sServerName;

                fileLocation m_Cert;
                fileLocation m_Key;

                std::string m_sStaticRootDir;
                endpoint m_ApiRoot;

                bool m_bWebsocket;
                mg_mgr m_mgr;
                unsigned long m_nPort;
                size_t m_nMaxConnections;

                std::chrono::milliseconds m_PollTimeout;

                std::function<void(std::chrono::milliseconds)> m_loopCallback;
                std::map<methodpoint, std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)>> m_mEndpoints;
                std::map<endpoint, std::function<bool(const endpoint&, const userName&, const ipAddress& peer)>> m_mWebsocketAuthenticationEndpoints;
                std::map<endpoint, std::function<bool(const endpoint&, const Json::Value&)>> m_mWebsocketMessageEndpoints;
                std::map<endpoint, std::function<void(const endpoint&, const ipAddress& peer)>> m_mWebsocketCloseEndpoints;
                std::multimap<endpoint, httpMethod> m_mmOptions;

                std::function<bool(const std::string&)> m_tokenCallback;


                std::map<mg_connection*, subscriber > m_mSubscribers;

                std::queue<wsMessage> m_qWsMessages;

                std::mutex m_mutex;
                bool m_bThreaded;
                std::condition_variable m_cvSync;

                response m_signal;

                std::atomic<bool> m_bLoop;
                std::unique_ptr<std::thread> m_pThread;
                std::function<response(const httpMethod&, const query&, const std::vector<partData>&, const endpoint&, const userName&)> m_callbackNotFound;

                std::map<userName, password> m_mUsers;

                struct httpchunks
                {
                    httpchunks() : nTotalSize(0), nCurrentSize(0), pCallback(nullptr), ePlace(BOUNDARY), pofs(nullptr){}
                    ~httpchunks();
                    size_t nTotalSize;
                    size_t nCurrentSize;
                    headerValue contentType;
                    methodpoint thePoint;
                    query theQuery;
                    std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)> pCallback;

                    userName theUser;
                    //multipart stuff
                    enum enumPlace{BOUNDARY, HEADER};
                    enumPlace ePlace;

                    std::vector<char> vBuffer;
                    std::string sBoundary;
                    std::string sBoundaryLast;
                    std::vector<partData> vParts;

                    std::shared_ptr<std::ofstream> pofs;
                };

                void HandleFirstChunk(httpchunks& chunk, mg_connection* pConnection, mg_http_message* pMessage);
                void HandleLastChunk(httpchunks& chunk, mg_connection* pConnection);
                void HandleMultipartChunk(httpchunks& chunk, mg_http_message* pMessage);
                void HandleGenericChunk(httpchunks& chunk, mg_http_message* pMessage);
                void WorkoutBoundary(httpchunks& chunk);
                void MultipartChunkBoundary(httpchunks& chunk, char c);
                void MultipartChunkHeader(httpchunks& chunk, char c);
                void MultipartChunkBoundaryFound(httpchunks& chunk, char c);
                void MultipartChunkLastBoundaryFound(httpchunks& chunk, char c);
                void MultipartChunkBoundarySearch(httpchunks& chunk, char c);

                std::map<mg_connection*, httpchunks> m_mChunks;

                std::map<mg_connection*, std::unique_ptr<std::ifstream> > m_mFileDownloads;
        };
    };
};
