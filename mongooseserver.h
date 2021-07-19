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


extern RG_EXPORT bool operator<(const endpoint& e1, const endpoint& e2);

struct multipartData
{
    std::map<endpoint, std::function<response(const query&, const postData&, const url&, const userName&)>>::const_iterator itEndpoint;
    std::map<std::string, std::string> mData;
    std::map<std::string, std::string> mFiles;
    std::ofstream ofs;
};

using wsMessage = std::pair<std::set<std::string>, Json::Value>;

using authorised = std::pair<bool, userName>;

class MongooseServer
{
    public:

        MongooseServer();
        ~MongooseServer();

        bool Init(const std::string& sCert, const std::string& sKey, int nPort, const std::string& sApiRoot);


        void AddBAUser(const userName& aUser, const password& aPassword);
        void DeleteBAUser(const userName& aUser);


        /** @brief Creates the thread that runs the webserver loop
        *   @param bThread if true will run in a separate thread, if false will run in main thread
        *   @param nTimeoutms the time in milliseconds to wait for a mongoose event to happen
        **/
        void Run(bool bThread, unsigned int nTimeoutMs=100);


        ///< @brief Stops the server
        void Stop();


        bool AddWebsocketEndpoint(const url& theEndpoint, std::function<bool(const url&, const userName&, const ipAddress& peer)> funcAuthentication, std::function<bool(const url&, const Json::Value&)> funcMessage, std::function<void(const url&, const ipAddress& peer)> funcClose);

        /** Adds a callback handler for an endpoint
        *   @param theEndpoint a pair definining the HTTP method and endpoint address
        *   @param func std::function that defines the callback function
        *   @return <i>bool</i> true on success
        **/
        bool AddEndpoint(const endpoint& theEndpoint, std::function<response(const query&, const postData&, const url&, const userName&)> func);

        /** Removes a callback handler for an endpoint
        *   @param theEndpoint a pair definining the HTTP method and endpoint address
        *   @return <i>bool</i> true on success
        **/
        bool DeleteEndpoint(const endpoint& theEndpoint);

        /** Sets the function that will be called every time the poll function times out or an event happens
        *   @param func the function to call. It will be passed one argument, the number of milliseconds since it was last called
        **/
        void SetLoopCallback(std::function<void(unsigned int)> func);

        void SendWebsocketMessage(const std::set<std::string>& setEndpoints, const Json::Value& jsMessage);

        /** Handles an event
        *   @param pConnection the mg_connection that caused the event
        *   @param nEvent the event type
        *   @param pData any associated data
        **/
        void HandleEvent(mg_connection *pConnection, int nEvent, void* pData);


        std::set<endpoint> GetEndpoints();


        void SetStaticDirectory(const std::string& sDir) { m_sStaticRootDir = sDir;}
        const std::string& GetStaticDirectory() const {return m_sStaticRootDir;}

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

        void EventHttpWebsocket(mg_connection *pConnection, mg_http_message* pMessage, const url& uri);
        void EventHttpApi(mg_connection *pConnection, mg_http_message* pMessage, const httpMethod& method, const url& uri);

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
        void SendAuthenticationRequest(mg_connection* pConnection);

        void SendOptions(mg_connection* pConnection, const std::string& sUrl);

        void SendWSQueue();
        void ClearMultipartData();

        authorised CheckAuthorization(struct mg_http_message* pMessage);

        struct subscriber
        {
            subscriber(const url& aUrl, const ipAddress& Ip) : theUrl(aUrl), peer(Ip), bAuthenticated(false){}
            url theUrl;
            ipAddress peer;
            bool bAuthenticated;
            std::set<std::string> setEndpoints;
        };

        void HandleInternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData);
        void HandleExternalWebsocketMessage(mg_connection* pConnection, subscriber& sub, const Json::Value& jsData);

        void AddWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData);
        void RemoveWebsocketSubscriptions(subscriber& sub, const Json::Value& jsData);

        bool AuthenticateWebsocket(subscriber& sub, const Json::Value& jsData);

        void HandleAccept(mg_connection* pConnection);


        bool InApiTree(const std::string& sUri);

        mg_connection* m_pConnection;
        std::string m_sIniPath;
        std::string m_sServerName;

        std::string m_sCert;
        std::string m_sKey;

        std::string m_sStaticRootDir;
        std::string m_sApiRoot;

        mg_mgr m_mgr;


        int m_nPollTimeout;

        std::function<void(unsigned int)> m_loopCallback;
        std::map<endpoint, std::function<response(const query&, const postData&, const url&, const userName&)>> m_mEndpoints;
        std::map<url, std::function<bool(const url&, const userName&, const ipAddress& peer)>> m_mWebsocketAuthenticationEndpoints;
        std::map<url, std::function<bool(const url&, const Json::Value&)>> m_mWebsocketMessageEndpoints;
        std::map<url, std::function<void(const url&, const ipAddress& peer)>> m_mWebsocketCloseEndpoints;
        std::multimap<std::string, httpMethod> m_mmOptions;



        std::map<mg_connection*, subscriber > m_mSubscribers;

        std::queue<wsMessage> m_qWsMessages;
        multipartData m_multipartData;

        std::mutex m_mutex;
        bool m_bThreaded;

        std::atomic<bool> m_bLoop;
        std::unique_ptr<std::thread> m_pThread;

        std::map<userName, password> m_mUsers;
};
