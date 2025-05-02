#ifndef PML_RESTGOOSE_RESTGOOSE
#define PML_RESTGOOSE_RESTGOOSE

#include <chrono>
#include <filesystem>
#include <functional>
#include <set>

#include "json/json.h"
#include "response.h"

namespace pml::restgoose
{
    class MongooseServer;

    class RG_EXPORT Server
    {
        public:
            Server();
            ~Server();


            /** @brief Initialises the server
            *   @param[in] ca the full path and file name to a TLS certificate authorirty
            *   @param[in] cert the full path and file name to a TLS certificate if one is being used
            *   @param[in] key the full path and file name to a TLC key if one is being used
            *   @param[in] addr the ip address of the interface to run on (pass 0.0.0.0 to listen on all interfaces)
            *   @param[in] nPort the TCP/IP port number to listen on if set to 0 then will use 80 for http and 443 for https
            *   @param[in] apiRoot the relative URL that is the base of the API tree
            *   @param[in] bEnableWebsocket set to true to act as a websocket server as well
            *   @param[in] bSendPings set to true for the server to send websocket pings to the clients
            *   @return bool true if the server has been successufully intialised
            **/
            bool Init(const std::filesystem::path& ca, const std::filesystem::path& cert, const std::filesystem::path& key, const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings=false);

            /** @brief Initialises the server
            *   @param[in] cert the full path and file name to a TLS certificate if one is being used
            *   @param[in] key the full path and file name to a TLC key if one is being used
            *   @param[in] addr the ip address of the interface to run on (pass 0.0.0.0 to listen on all interfaces)
            *   @param[in] nPort the TCP/IP port number to listen on
            *   @param[in] apiRoot the relative URL that is the base of the API tree
            *   @param[in] bEnableWebsocket set to true to act as a websocket server as well
            *   @param[in] bSendPings set to true for the server to send websocket pings to the clients
            *   @return bool true if the server has been successufully intialised
            **/
            bool Init(const std::filesystem::path& cert, const std::filesystem::path& key, const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings=false);

            /** @brief Initialises the server
            *   @param[in] addr the ip address of the interface to run on (pass 0.0.0.0 to listen on all interfaces)
            *   @param[in] nPort the TCP/IP port number to listen on
            *   @param[in] apiRoot the relative URL that is the base of the API tree
            *   @param[in] bEnableWebsocket set to true to act as a websocket server as well
            *   @return bool true if the server has been successufully intialised
            **/
            bool Init(const ipAddress& addr, unsigned short nPort, const endpoint& apiRoot, bool bEnableWebsocket, bool bSendPings=false);

            /** @brief Runs the webserver
            *   @param[in] bThread if true will run in a separate thread, if false will run in main thread
            *   @param[in] nTimeoutms the time in milliseconds to wait for a mongoose event to happen
            **/
            void Run(bool bThread, const std::chrono::milliseconds& timeout=std::chrono::milliseconds(100));

            /** @brief Sets the interface and port to run on. If the webserver is already running this will have no effect until it is restarted
            *   @param[in] addr the ip address of the interface to run on (pass 0.0.0.0 to listen on all interfaces)
            *   @param[in] nPort the TCP/IP port number to listen on
            **/
            void SetInterface(const ipAddress& addr, unsigned short nPort);

            /** @brief Sets the authorization type to expect a bearer token in the Authorization header
            *   @param[in] callback a callback function that should inspect the token and return true if authorization is allowed
            *   @param[in] bAuthenticateWebsocketsViaQuery if true then, for websocket connections, the bearer token is expected to be passed as a query param in the connecting websocket url (e.g. wss://127.0.0.1:/ws/api?jwt=923834aa9q3....). If false then the token must be passed as the first message from the client to the server
            **/
            void SetAuthorizationTypeBearer(const std::function<bool(const methodpoint&,const std::string& theToken)>& callback, const std::function<response(const endpoint&, bool)>& callbackHandleNotAuthorized, bool bAuthenticateWebsocketsViaQuery);

            /** @brief Sets the authorization type to basic authentication
            *   @param[in] aUser a user name
            *   @param[in] aPassword the password that authenticates the user
            **/
            void SetAuthorizationTypeBasic(const userName& aUser, const password& aPassword);

            /** @brief Removes all authorization
            **/
            void SetAuthorizationTypeNone();

            /** @brief Define a set of methodpoints that do not need to be authenticated (e.g. to show an initial login page)
            *   @param[in] setUnprotected a set of method,endpoint pairs
            **/
            void SetUnprotectedEndpoints(const std::set<methodpoint>& setUnprotected);

            /** @brief Adds a basic authentication user/password pair to the server
            *   @param[in] aUser the username
            *   @param[in] aPassword the password
            *   @return bool will return false if the authorization type is not set to basic or the user already exists
            **/
            bool AddBAUser(const userName& aUser, const password& aPassword);

            /** @brief Deletes a user from basic authentication
            *   @param[in] aUser the username
            *   @return bool will return false if the authorization type is not set to basic
            **/
            bool DeleteBAUser(const userName& aUser);

            /** @brief Adds an enpoint that a websocket client can connect to along with callback functions
            *   @param[in] theEndpoint the address the client is allowed to connect to
            *   @param[in] funcAuthentication a function that will be called when a client first attempts to connect to the methodpoint. The function is passed the methodpoint address, a map of any query paramaters in the url,
            the username that has been passed by the websocket and the ip address of the connecting client. The function should return true to allow the connection to continue and false to close it
            *   @param[in] funcMessage a function that is called everytime the client sends a websocket message to the server. The function is passed the methodpoint address and the message passed (as Json).
                The function should return true to allow the connection to continue and false to close it
            *  @param[in] funcClose a function that is called when the client closes the websocket connection. The function is passed the methodpoint address and the client ip address
            *  @return bool true if the websocket methodpoint was added
                **/
            bool AddWebsocketEndpoint(const endpoint& theEndpoint, const std::function<bool(const endpoint&, const query&, const userName&, const ipAddress&)>& funcAuthentication, const std::function<bool(const endpoint&, const Json::Value&)>& funcMessage, const std::function<void(const endpoint&, const ipAddress&)>& funcClose);

            /** @brief Adds a function to be called everytime a client attempts to connect to a methodpoint that is not defined
            *   @param[in] func the function to be called. The function is passed the query data, std::vector<partData> (for a PUT,PATCH or POST) the methodpoint and the userName if any.
            The function should return a response which will be sent back to the client
            **/
            void AddNotFoundCallback(const std::function<response(const httpMethod&, const query&, const std::vector<partData>&, const endpoint&, const userName&)>& func);

            ///< @brief Stops the server
            void Stop();


            /** @brief Set the maximum number of connections the server will accept
            *   @param[in] nMax the maximum number of connections
            **/
            void SetMaxConnections(size_t nMax);

            /** @brief Sets the access control list for the server
            *   @param[in] sAcl the access control list
            *   @note the form is -[not allowed],+[allowed]
            **/
            void SetAccessControlList(const std::string& sAcl);

            /** @brief Get the access control list
             *  @return Comma separated acces control list
             *  @note the form is -[not allowed],+[allowed]
            **/
            const std::string& GetAccessControlList() const;

            /** Adds a callback handler for an methodpoint
            *   @param[in] theEndpoint a pair definining the HTTP method and methodpoint address
            *   @param[in] func std::function that defines the callback function.The function is passed the query data, std::vector<partData> (for a PUT,PATCH or POST) the methodpoint and the userName if any.
            *   @param[in] bUseThread if false then the callback will be called in the server thread
            The function should return a response which will be sent back to the client
            *   @return bool true on success
            **/
            bool AddEndpoint(const httpMethod& method, const endpoint& theEndpoint, const std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)>& func, bool bUseThread = false);

            /** Removes a callback handler for an methodpoint
            *   @param[in] theEndpoint a pair definining the HTTP method and methodpoint address
            *   @return bool true on success
            **/
            bool DeleteEndpoint(const httpMethod& method, const endpoint& theEndpoint);

            /** Sets the function that will be called every time the poll function times out or an event happens
            *   @param[in] func the function to call. It will be passed one argument, the number of milliseconds since it was last called
            **/
            void SetLoopCallback(const std::function<void(std::chrono::milliseconds)>& func);

            /** @brief Sends a JSON formatted websocket message
            *   @param[in] setEnpoints the set of websocket methodpoints that the message should be sent to
            *   @param[in] jsMessage the message to send
            **/
            void SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage);

            /** @brief Gets a set containing all the defined Restful methodpoints
            *   @return set<methodpoint> the methodpoints
            **/
            std::set<methodpoint> GetEndpoints();

            /** @brief Set a directory to serve standard webpages from
            *   @param[in] sDir a full path to the directory that the static html web pages live in
            **/
            void SetStaticDirectory(const std::string& sDir);

            /** @brief Gets the directory that has been set to serve html web pages from
            *   @return string the full path
            **/
            const std::string& GetStaticDirectory() const;

            /** @brief Gets the port number that the server is listening on
            *   @return unsigned long the port
            **/
            unsigned long GetPort() const;


            /** @brief Primes the server for pausing - allowing another thread to gather data before the server replies
            **/
            void PrimeWait();

            /** @brief Pauses the server thread - allowing another thread to gather data before the server replies
            **/
            void Wait();


            /** @brief Restarts a paused server thread
            *   @param[in] resp a response object containing data from the signalling thread
            *   @note this must be called from another thread
            **/
            void Signal(const response& resp);


            /** @brief Get the response value passed in the Signal routine
            *   @return response the data sent from the signalling thread
            **/
            const response& GetSignalResponse() const;


            /** @brief Gets the ip address of the peer that sent the request currently being handled
            *   @param[in] bIncludePort if true then the port number is included else just the ip address
            *   @note this is address is only valid whilst handling an api endpoint callback
            **/
            const ipAddress& GetCurrentPeer(bool bIncludePort = true) const;

            /**
             * @brief Get the Number Of Websocket Connections object
             * 
             * @return size_t the number of websocket connections
             */
            size_t GetNumberOfWebsocketConnections() const;

            /** @brief Adds extra headers for the server to send on all replies
            *   @param[in] mHeaders a map of headerName, headerValue pairs
            *   @note if a headerName already exists in list of those to be sent then the value sent is overwritten by this function
            *   @note Content-Type and Content-Length are always sent and cannot be overwritten
            **/
            void AddHeaders(const std::map<headerName, headerValue>& mHeaders);

            /** @brief Removes headers from the list of those to be sent by the server
            * 
            *   @param[in] setHeaders a set of headerNames to be removed
            *   @note Content-Type and Content-Length are always sent and cannot be removed
            **/
            void RemoveHeaders(const std::set<headerName>& setHeaders);

            /** @brief Sets the headers to be sent by the server
            *   @para[in]m mHeaders a map of headerName, headerValue pairs
            *   @note Content-Type and Content-Length are always sent
            **/
            void SetHeaders(const std::map<headerName, headerValue>& mHeaders);

        private:
            std::unique_ptr<MongooseServer> m_pImpl;
    };
}

#endif
