#pragma once
#include "response.h"
#include "json/json.h"
#include <set>


class MongooseServer;

class RG_EXPORT RestGoose
{
    public:
        RestGoose();
        ~RestGoose();

          bool Init(const std::string& sCert, const std::string& sKey, int nPort);

        /** @brief Creates the thread that runs the webserver loop
        *   @param bThread if true will run in a separate thread, if false will run in main thread
        *   @param nTimeoutms the time in milliseconds to wait for a mongoose event to happen
        **/
        void Run(bool bThread, unsigned int nTimeoutMs=100);


        /** @brief Adds a basic authentication user/password pair to the server
        *   @param sUser the username
        *   @param sPassword the password
        **/
        void AddBAUser(const userName& aUser, const password& aPassword);
        void DeleteBAUser(const userName& aUser);

        bool AddWebsocketEndpoint(const url& theEndpoint, std::function<bool(const url&, const userName&, const ipAddress& peer)> funcAuthentication, std::function<bool(const url&, const Json::Value&)> funcMessage, std::function<void(const url&, const ipAddress& peer)> funcClose);


        ///< @brief Stops the server
        void Stop();

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

        std::set<endpoint> GetEndpoints();

        void SetStaticDirectory(const std::string& sDir);
        const std::string& GetStaticDirectory() const;

        static const httpMethod GET;
        static const httpMethod POST;
        static const httpMethod PUT;
        static const httpMethod PATCH;
        static const httpMethod HTTP_DELETE;
        static const httpMethod OPTIONS;

    private:
        std::unique_ptr<MongooseServer> m_pImpl;
};
