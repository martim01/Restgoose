#pragma once
#include "response.h"
#include "json/json.h"
#include <set>
#include <functional>

class MongooseServer;

class RG_EXPORT RestGoose
{
    public:
        RestGoose();
        ~RestGoose();

        bool Init(const std::string& sCert, const std::string& sKey, int nPort, const std::string& sApiRoot, bool bEnableWebsocket);

        /** @brief Creates the thread that runs the webserver loop
        *   @param bThread if true will run in a separate thread, if false will run in main thread
        *   @param nTimeoutms the time in milliseconds to wait for a mongoose event to happen
        **/
        void Run(bool bThread, unsigned int nTimeoutMs=100);


        /** @brief Adds a basic authentication user/password pair to the server
        *   @param aUser the username
        *   @param aPassword the password
        **/
        void AddBAUser(const userName& aUser, const password& aPassword);

        /** @brief Deletes a user from basic authentication
        *   @param aUser the username
        **/
        void DeleteBAUser(const userName& aUser);

        /** @brief Adds an enpoint that a websocket client can connect to along with callback functions
        *   @param theEnpoint the address the client is allowed to connect to
        *   @param funcAuthentication a function that will be called when a client first attempts to connect to the endpoint. The function is passed the endpoint address,
        the username that has been passed by the websocket and the ip address if the connecting client. The function should return true to allow the connection to continue and false to close it
        *   @param funcMessage a function that is called everytime the client sends a websocket message to the server. The function is passed the endpoint address and the message passed (as Json).
         The function should return true to allow the connection to continue and false to close it
         *  @param funcClose a function that is called when the client closes the websocket connection. The function is passed the endpoint address and the client ip address
         *  @return <i>bool</i> true if the websocket endpoint was added
         **/
        bool AddWebsocketEndpoint(const url& theEndpoint, std::function<bool(const url&, const userName&, const ipAddress& peer)> funcAuthentication,
        std::function<bool(const url&, const Json::Value&)> funcMessage, std::function<void(const url&, const ipAddress& peer)> funcClose);

        /** @brief Adds a function to be called everytime a client attempts to connect to a endpoint that is not defined
        *   @param func the function to be called. The function is passed the query data, postData (for a PUT,PATCH or POST) the endpoint and the userName if any.
        The function should return a response which will be sent back to the client
        **/
        void AddNotFoundCallback(std::function<response(const query&, const postData&, const url&, const userName&)> func);

        ///< @brief Stops the server
        void Stop();

        /** Adds a callback handler for an endpoint
        *   @param theEndpoint a pair definining the HTTP method and endpoint address
        *   @param func std::function that defines the callback function.The function is passed the query data, postData (for a PUT,PATCH or POST) the endpoint and the userName if any.
        The function should return a response which will be sent back to the client
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

        /** @brief Sends a JSON formatted websocket message
        *   @param setEnpoints the set of websocket endpoints that the message should be sent to
        *   @param jsMessage the message to send
        **/
        void SendWebsocketMessage(const std::set<std::string>& setEndpoints, const Json::Value& jsMessage);

        /** @brief Gets a set containing all the defined Restful endpoints
        *   @return <i>set<endpoint> the endpoints
        **/
        std::set<endpoint> GetEndpoints();

        /** @brief Set a directory to serve standard webpages from
        *   @param sDir a full path to the directory that the static html web pages live in
        **/
        void SetStaticDirectory(const std::string& sDir);

        /** @brief Gets the directory that has been set to serve html web pages from
        *   @return <i>string</i> the full path
        **/
        const std::string& GetStaticDirectory() const;

        /** @brief Gets the port number that the server is listening on
        *   @return <i>unsigned long</i> the port
        **/
        unsigned long GetPort() const;


        /** @brief Primes the server for pausing - allowing another thread to gather data before the server replies
        **/
        void PrimeWait();

        /** @brief Pauses the server thread - allowuing another thread to gather data before the server replies
        **/
        void Wait();


        /** @brief Restarts a paused server thread
        *   @param bOk true/false
        *   @param sData signalling data
        *   @note this must be called from another thread
        **/
        void Signal(bool bOk, const std::string& sData);

        /** @brief Get the boolean value passed in the Signal routine
        **/
        bool IsOk();

        /** @brief Get the string value passed in the Signal routine
        **/
        const std::string& GetSignalData();


        static const httpMethod GET;
        static const httpMethod POST;
        static const httpMethod PUT;
        static const httpMethod PATCH;
        static const httpMethod HTTP_DELETE;
        static const httpMethod OPTIONS;

    private:
        std::unique_ptr<MongooseServer> m_pImpl;
};
