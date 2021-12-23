#pragma once
#include <string>
#include "namedtype.h"
#include <functional>
#include <memory>
#include "response.h"

namespace pml
{
    namespace restgoose
    {
        class WebSocketClientImpl;

        /** @class A class that acts as a websocket client which can connect to one or more endpoints
        **/
        class RG_EXPORT WebSocketClient
        {
            public:

                /** @brief Constructor
                *   @param pConnectCallback a function that is called when the client connection to the server changes. It is passed the relative url of the connection and a boolean which is true on connection and false on disconnection. It should return true to keep the connection alive and false to close it
                *   @param pMessageCallback a function that is called whenever the server sends a message to the client. It is passed the relative url of the connection and a string containing the websocket message. It should return true to keep the connection alive and false to close it
                *   @param timeout the amount of time to wait for the connection
                **/
                WebSocketClient(std::function<bool(const endpoint&, bool)> pConnectCallback, std::function<bool(const endpoint&, const std::string&)> pMessageCallback, unsigned int nTimeout=250);
                ~WebSocketClient();

                /** @brief Starts the websocket client if it is not already running
                *   @return <i>bool</i> returns true if the websocket can be started and false if it was already running
                **/
                bool Run();

                /** @brief Stops the websocket client
                **/
                void Stop();

                /** @brief Attempts to initiate a connection to the given endpoint
                *   @param theEndpoint the absolute url to connect to
                *   @return <i>bool</i> return true on successful intitiation (not connection) and false if the connection already exists or could not be intialised
                **/
                bool Connect(const endpoint& theEndpoint);

                /** @brief Attempts to send a message to the given endpoint
                *   @param theEndpoint the absolute url to send the message to
                *   @param sMessage the message to send
                *   @return <i>bool</i> returns true if the endpoint has an active connection and false if not
                **/
                bool Send(const endpoint& theEndpoint, const std::string& sMessage);

                /** @brief Closes a websocket connection to the given endpoint
                *   @param theEndpoint the absolute url
                **/
                void CloseConnection(const endpoint& theEndpoint);

            private:
                std::unique_ptr<WebSocketClientImpl> m_pImpl;
        };
    }
}
