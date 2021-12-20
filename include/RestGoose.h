#pragma once
#include "response.h"
#include "json/json.h"
#include <set>
#include <functional>
#include <chrono>


namespace pml
{
    namespace restgoose
    {
        class MongooseServer;

        class RG_EXPORT Server
        {
            public:
                Server();
                ~Server();

                bool Init(const fileLocation& cert, const fileLocation& key, int nPort, const endpoint& apiRoot, bool bEnableWebsocket);

                /** @brief Creates the thread that runs the webserver loop
                *   @param bThread if true will run in a separate thread, if false will run in main thread
                *   @param nTimeoutms the time in milliseconds to wait for a mongoose event to happen
                **/
                void Run(bool bThread, const std::chrono::milliseconds& timeout=std::chrono::milliseconds(100));


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
                *   @param funcAuthentication a function that will be called when a client first attempts to connect to the methodpoint. The function is passed the methodpoint address,
                the username that has been passed by the websocket and the ip address if the connecting client. The function should return true to allow the connection to continue and false to close it
                *   @param funcMessage a function that is called everytime the client sends a websocket message to the server. The function is passed the methodpoint address and the message passed (as Json).
                 The function should return true to allow the connection to continue and false to close it
                 *  @param funcClose a function that is called when the client closes the websocket connection. The function is passed the methodpoint address and the client ip address
                 *  @return <i>bool</i> true if the websocket methodpoint was added
                 **/
                bool AddWebsocketEndpoint(const endpoint& theEndpoint, std::function<bool(const endpoint&, const userName&, const ipAddress&)> funcAuthentication,
                std::function<bool(const endpoint&, const Json::Value&)> funcMessage, std::function<void(const endpoint&, const ipAddress&)> funcClose);

                /** @brief Adds a function to be called everytime a client attempts to connect to a methodpoint that is not defined
                *   @param func the function to be called. The function is passed the query data, std::vector<partData> (for a PUT,PATCH or POST) the methodpoint and the userName if any.
                The function should return a response which will be sent back to the client
                **/
                void AddNotFoundCallback(std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)> func);

                ///< @brief Stops the server
                void Stop();

                /** Adds a callback handler for an methodpoint
                *   @param theEndpoint a pair definining the HTTP method and methodpoint address
                *   @param func std::function that defines the callback function.The function is passed the query data, std::vector<partData> (for a PUT,PATCH or POST) the methodpoint and the userName if any.
                The function should return a response which will be sent back to the client
                *   @return <i>bool</i> true on success
                **/
                bool AddEndpoint(const httpMethod& method, const endpoint& theEndpoint, std::function<response(const query&, const std::vector<partData>&, const endpoint&, const userName&)> func);

                /** Removes a callback handler for an methodpoint
                *   @param theEndpoint a pair definining the HTTP method and methodpoint address
                *   @return <i>bool</i> true on success
                **/
                bool DeleteEndpoint(const httpMethod& method, const endpoint& theEndpoint);

                /** Sets the function that will be called every time the poll function times out or an event happens
                *   @param func the function to call. It will be passed one argument, the number of milliseconds since it was last called
                **/
                void SetLoopCallback(std::function<void(std::chrono::milliseconds)> func);

                /** @brief Sends a JSON formatted websocket message
                *   @param setEnpoints the set of websocket methodpoints that the message should be sent to
                *   @param jsMessage the message to send
                **/
                void SendWebsocketMessage(const std::set<endpoint>& setEndpoints, const Json::Value& jsMessage);

                /** @brief Gets a set containing all the defined Restful methodpoints
                *   @return <i>set<methodpoint> the methodpoints
                **/
                std::set<methodpoint> GetEndpoints();

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
                void Signal(const response& resp);


                /** @brief Get the string value passed in the Signal routine
                **/
                const response& GetSignalResponse() const;




            private:
                std::unique_ptr<MongooseServer> m_pImpl;
        };
    }
}
