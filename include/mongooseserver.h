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
#include "safequeue.h"
#include <filesystem>

extern RG_EXPORT bool operator<(const methodpoint& e1, const methodpoint& e2);
extern size_t GetNumberOfConnections(mg_mgr& mgr);
extern size_t DoGetNumberOfWebsocketConnections(const mg_mgr& mgr);

using wsMessage = std::pair<std::set<endpoint>, Json::Value>;
using authorised = std::pair<bool, userName>;



struct end_less
{
    bool operator() (endpoint e1, endpoint e2) const;
};



namespace pml
{
    namespace restgoose
    {
        using endpointCallback = std::pair<std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)>, bool>;

        class Server;
        class MongooseServer
        {
            public:
                friend class Server;

                bool Init(const std::filesystem::path& ca, const std::filesystem::path& cert, const std::filesystem::path& key, const ipAddress& addr,  unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings);

                void SetInterface(const ipAddress& addr, unsigned short nPort);

                void SetAuthorizationTypeBearer(const std::function<bool(const methodpoint&, const std::string&)>& callback, const std::function<response(const endpoint&, bool)>&  callbackHandleNotAuthorized,bool bAuthenticateWebsocketsViaQuery);
                void SetAuthorizationTypeBasic(const userName& aUser, const password& aPassword);
                void SetAuthorizationTypeNone();
                void SetUnprotectedEndpoints(const std::set<methodpoint>& setUnprotected);

                bool AddBAUser(const userName& aUser, const password& aPassword);
                bool DeleteBAUser(const userName& aUser);


                void SetMaxConnections(size_t nMax);

                void SetAccessControlList(std::string_view sAcl) { m_sAcl = sAcl; }
                const std::string& GetAccessControlList() const { return m_sAcl;}

                /** @brief Creates the thread that runs the webserver loop
                *   @param bThread if true will run in a separate thread, if false will run in main thread
                *   @param nTimeoutms the time in milliseconds to wait for a mongoose event to happen
                **/
                void Run(bool bThread, const std::chrono::milliseconds& timeout);


                ///< @brief Stops the server
                void Stop();


                bool AddWebsocketEndpoint(const endpoint& theEndpoint, const std::function<bool(const endpoint&, const query&, const userName&, const ipAddress&)>& funcAuthentication,
                                          const std::function<bool(const endpoint&, const Json::Value&)>& funcMessage,
                                          const std::function<void(const endpoint&, const ipAddress&)>& funcClose);

                /** Adds a callback handler for an methodpoint - this method will run in the same thread as the server thread
                *   @param theMethodPoint a pair definining the HTTP method and methodpoint address
                *   @param func std::function that defines the callback function
                *   @param bUseThread if false then the callback will be called in the server thread
                *   @return <i>bool</i> true on success
                **/
                bool AddEndpoint(const methodpoint& theMethodPoint, const std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)>& func, bool bUseThread=false);

                /** @brief Adds a callback handler that is called if no handler is found for the endpoint
                **/
                void AddNotFoundCallback(const std::function<response(const httpMethod&, const query&, const std::vector<partData>&, const endpoint&, const userName&)>& func);

                /** Removes a callback handler for an methodpoint
                *   @param theMethodPoint a pair definining the HTTP method and methodpoint address
                *   @return <i>bool</i> true on success
                **/
                bool DeleteEndpoint(const methodpoint& theMethodPoint);

                /** Sets the function that will be called every time the poll function times out or an event happens
                *   @param func the function to call. It will be passed one argument, the number of milliseconds since it was last called
                **/
                void SetLoopCallback(const std::function<void(std::chrono::milliseconds)>& func);

                void SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage);


                void AddHeaders(const std::map<headerName, headerValue>& mHeaders);
                void RemoveHeaders(const std::set<headerName>& setHeaders);
                void SetHeaders(const std::map<headerName, headerValue>& mHeaders);

                std::set<methodpoint> GetEndpoints() const;


                void SetStaticDirectory(std::string_view sDir);
                const std::string& GetStaticDirectory() const {return m_sStaticRootDir;}

                unsigned long GetPort() const { return m_nPort; }

                size_t GetNumberOfWebsocketConnections() const;

                void Wait();
                void PrimeWait();
                //bool IsOk();
                void Signal(const response& resp);
                const response& GetSignalResponse() const;


                /** Handles an event
                *   @param pConnection the mg_connection that caused the event
                *   @param nEvent the event type
                *   @param pData any associated data
                **/
                void HandleEvent(mg_connection *pConnection, int nEvent, void* pData);

                void SendWSQueue();

                const ipAddress& GetCurrentPeer(bool bIncludePort = true) const;

                ~MongooseServer();

            protected:

                MongooseServer();


            private:

                void CloseWebsocket(mg_connection* pConnection);

                ///< @brief the main mongoose loop. Called in a separate thread by Run()
                void Loop();

                /** @brief Handles all Mongoose websocket events
                *   @param pConnection the mg_connection that caused the event
                *   @param nEvent the event type
                *   @param pData any associated data
                **/
                void EventWebsocketOpen(mg_connection const* pConnection, int nEvent, void* pData) const;

                void EventWebsocketMessage(mg_connection* pConnection, int nEvent, void* pData);
                void EventWebsocketCtl(mg_connection* pConnection, int nEvent, void* pData);


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


                std::string CreateHeaders(const response& theResponse, size_t nLength) const;

                void DoReply(mg_connection* pConnection, const response& theResponse);
                void DoReplyText(mg_connection* pConnection, const response& theResponse) const;
                void DoReplyFile(mg_connection* pConnection, const response& theResponse);
                void DoReplyThreaded(mg_connection* pConnection, const query& theQuery, const std::vector<partData>& theData, const methodpoint& thePoint, const userName& theUser);
                void SendAuthenticationRequest(mg_connection* pConnection, const methodpoint& thePoint, bool bApi);

                void SendOptions(mg_connection* pConnection, const endpoint& thEndpoint);


                //void ClearMultipartData();

                authorised CheckAuthorization(struct mg_http_message* pMessage);
                authorised CheckAuthorizationBasic(struct mg_http_message* pMessage);
                authorised CheckAuthorizationBearer(const methodpoint& thePoint, struct mg_http_message* pMessage);

                void HandleThreadedMessage(mg_connection* pConnection);


                struct subscriber
                {
                    subscriber(const endpoint& anEndpoint, const ipAddress& Ip, const query& q) : theEndpoint(anEndpoint), peer(Ip), queryParams(q){}
                    endpoint theEndpoint;
                    ipAddress peer;
                    query queryParams;
                    bool bAuthenticated = false;
                    bool bPonged = true;
                    std::set<endpoint> setEndpoints;
                };

                bool WebsocketSubscribedToEndpoint(const subscriber& sub, const endpoint& anEndpoint) const;

                void HandleInternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData);
                void HandleExternalWebsocketMessage(mg_connection* pConnection, const subscriber& sub, const Json::Value& jsData);

                void AddWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData) const;
                void RemoveWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData) const;


                void DoWebsocketAuthentication(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData);
                bool AuthenticateWebsocket(const subscriber& sub, const Json::Value& jsData);
                bool AuthenticateWebsocketBasic(const subscriber& sub, const Json::Value& jsData);
                bool AuthenticateWebsocketBearer(const subscriber& sub, const Json::Value& jsData);
                bool MethodPointUnprotected(const methodpoint& thePoint);

                void HandleAccept(mg_connection* pConnection) const;
                void HandleOpen(mg_connection* pConnection);

                void EventHttpChunk(mg_connection *pConnection, void* pData);

                methodpoint GetMethodPoint(mg_http_message* pMessage) const;

                bool InApiTree(const endpoint& theEndpoint);

                void SendAndCheckPings(const std::chrono::milliseconds& elapsed);

                mg_connection* m_pConnection = nullptr;
                int m_nPipe =0;
                std::string m_sIniPath;
                std::string m_sServerName;

                std::filesystem::path m_Cert;
                std::filesystem::path m_Key;
                std::filesystem::path m_Ca;

                std::string m_sStaticRootDir;
                endpoint m_ApiRoot;

                bool m_bWebsocket = false;
                bool m_bSendPings = false;
                bool m_bAuthenticateWSViaQuery = false;
                mg_mgr m_mgr;
                unsigned long m_nPort = 80;
                size_t m_nMaxConnections = std::numeric_limits<size_t>::max();

                std::chrono::milliseconds m_PollTimeout = std::chrono::milliseconds(100);

                std::function<void(std::chrono::milliseconds)> m_loopCallback = nullptr;

                std::map<methodpoint, endpointCallback> m_mEndpoints;
                std::map<endpoint, std::function<bool(const endpoint&, const query&, const userName&, const ipAddress& peer)>, end_less> m_mWebsocketAuthenticationEndpoints;
                std::map<endpoint, std::function<bool(const endpoint&, const Json::Value&)>, end_less> m_mWebsocketMessageEndpoints;
                std::map<endpoint, std::function<void(const endpoint&, const ipAddress& peer)>, end_less> m_mWebsocketCloseEndpoints;
                std::multimap<endpoint, httpMethod, end_less> m_mmOptions;

                std::function<bool(const methodpoint&, const std::string&)> m_tokenCallback = nullptr;
                std::function<response(const endpoint&, bool)> m_tokenCallbackHandleNotAuthorized = nullptr;

                std::map<mg_connection*, subscriber > m_mSubscribers;

                std::queue<wsMessage> m_qWsMessages;

                std::map<mg_connection*, thread_safe_queue<response>> m_mConnectionQueue;

                std::mutex m_mutex;
                bool m_bThreaded = true;
                std::condition_variable m_cvSync;

                response m_signal;

                std::atomic_bool m_bLoop = ATOMIC_VAR_INIT(true);
                std::unique_ptr<std::thread> m_pThread = nullptr;
                std::function<response(const httpMethod&, const query&, const std::vector<partData>&, const endpoint&, const userName&)> m_callbackNotFound = nullptr;

                std::map<userName, password> m_mUsers;

                std::set<methodpoint> m_setUnprotected;

                std::chrono::milliseconds m_timeSinceLastPingSent = std::chrono::milliseconds(0);

                ipAddress m_lastPeer;
                ipAddress m_lastPeerAndPort;

                std::string m_sAcl;

                struct httpchunks
                {
                    httpchunks()=default;
                    ~httpchunks();
                    size_t nTotalSize = 0;
                    size_t nCurrentSize =0;
                    headerValue contentType;
                    methodpoint thePoint;
                    query theQuery;
                    endpointCallback pCallback = {nullptr, false};

                    userName theUser;
                    //multipart stuff
                    enum enumPlace{BOUNDARY, HEADER};
                    enumPlace ePlace = BOUNDARY;

                    std::vector<char> vBuffer;
                    std::string sBoundary;
                    std::string sBoundaryLast;
                    std::vector<partData> vParts;

                    std::shared_ptr<std::ofstream> pofs = nullptr;
                };

                void HandleFirstChunk(httpchunks& chunk, mg_connection* pConnection, mg_http_message* pMessage);
                void HandleLastChunk(httpchunks& chunk, mg_connection* pConnection);
                void HandleMultipartChunk(httpchunks& chunk, mg_http_message* pMessage);
                void HandleGenericChunk(httpchunks& chunk, mg_http_message* pMessage) const;
                void WorkoutBoundary(httpchunks& chunk) const;
                void MultipartChunkBoundary(httpchunks& chunk, char c) const;
                void MultipartChunkHeader(httpchunks& chunk, char c) const;
                void MultipartChunkBoundaryFound(httpchunks& chunk, char c) const;
                void MultipartChunkLastBoundaryFound(httpchunks& chunk, char c) const;
                void MultipartChunkBoundarySearch(httpchunks& chunk, char c) const;

                std::map<mg_connection*, httpchunks> m_mChunks;

                std::map<mg_connection*, std::unique_ptr<std::ifstream> > m_mFileDownloads;

                std::map<headerName, headerValue> m_mHeaders;

                std::string m_sHostname;
        };
    };
};
