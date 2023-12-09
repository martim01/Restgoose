#pragma once
#include <vector>
#include <functional>
#include <chrono>
#include <memory>
#include "response.h"
#include <filesystem>

namespace pml
{
    namespace restgoose
    {
        class HttpClientImpl;

        /** @brief Class to connect to HTTP server
        **/
        class RG_EXPORT HttpClient
        {
            public:


                /** @brief Constructor - creates an HttpClient object that does not send any data to the server. Usually used for GET, DELETE or OPTIONS
                *   @param method the HTTP action - one of GET, POST, PUT, PATCH, DELETE, OPTIONS
                *   @param target the absolute url to connect to
                *   @param mExtraHeaders map of extra headers to send.
                *   @param eResponse one of clientResponse::enumResponse::TEXT, FILE or AUTO, decides whether the response data should be saved to a file or stored in the clientResponse data variable. If set to AUTO then the content-type header is looked at.
                **/
                HttpClient(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);

                /** @brief Constructor - creates an HttpClient object that sends JSON formatted data to the server
                *   @param method the HTTP action - one of POST, PUT, PATCH
                *   @param target the absolute url to connect to
                *   @param jsData the data to send to the server in JSON format
                *   @param mExtraHeaders map of extra headers to send.
                *   @param eResponse one of clientResponse::enumResponse::TEXT, FILE or AUTO, decides whether the response data should be saved to a file or stored in the clientResponse data variable. If set to AUTO then the content-type header is looked at.
                **/
                HttpClient(const httpMethod& method, const endpoint& target, const Json::Value& jsData, const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);

                /** @brief Constructor - creates an HttpClient object that sends arbitrary formatted data to the server
                *   @param method the HTTP action - one of POST, PUT, PATCh
                *   @param target the absolute url to connect to
                *   @param data the data to send to the server
                *   @param contentType the media type of the data being sent to the server
                *   @param mExtraHeaders map of extra headers to send.
                *   @param eResponse one of clientResponse::enumResponse::TEXT, FILE or AUTO, decides whether the response data should be saved to a file or stored in the clientResponse data variable. If set to AUTO then the content-type header is looked at.
                **/
                HttpClient(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType = headerValue("text/plain"), const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);

                /** @brief Constructor - creates an HttpClient object that will upload a file to the server
                *   @param method the HTTP action - one of POST, PUT, PATCH
                *   @param target the absolute url to connect to
                *   @param filename the name of the file being sent to the server
                *   @param filepath the location of the file
                *   @param contentType the media type of the data being sent to the server
                *   @param mExtraHeaders map of extra headers to send.
                *   @param eResponse one of clientResponse::enumResponse::TEXT, FILE or AUTO, decides whether the response data should be saved to a file or stored in the clientResponse data variable. If set to AUTO then the content-type header is looked at.
                **/
                HttpClient(const httpMethod& method, const endpoint& target, const textData& filename, const std::filesystem::path& filepath, const headerValue& contentType = headerValue("application/octet-stream"), const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);

                /** @brief Constructor - creates an HttpClient object that sends multipart/form data to the server
                *   @param method the HTTP action - one of POST, PUT, PATCH
                *   @param target the absolute url to connect to
                *   @param vData vector of partData objects that define the multipart data
                *   @param contentType the media type of the data being sent to the server
                *   @param mExtraHeaders map of extra headers to send.
                *   @param eResponse one of clientResponse::enumResponse::TEXT, FILE or AUTO, decides whether the response data should be saved to a file or stored in the clientResponse data variable. If set to AUTO then the content-type header is looked at.
                **/
                HttpClient(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO); //multipart
                ~HttpClient();

                /** @brief The function that attempts to connect to the server, send any data and retrieve any response. This is a synchronous function
                *   @param connectionTimeout the amount of time to wait for a connection to the server to be accepted before giving up
                *   @param processTimeout the amount of time to wait for the whole connect, send, receive procedure to take place. If set to 0 then the timeout is ignored
                *   @return <i>clientResponse</i> a clientResponse object containing the HTTP repsonse code and any data sent from the server
                **/
                const clientResponse& Run(const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0)) const;

                /** @brief The function that attempts to connect to the server, send any data and retrieve any response. This is an asynchronous function
                *   @param pCallback a function to call when the procedure is complete, its arguments are a clientResponse object containing the repsonse from the server, an unsigned int containing the value passed in nRunId and a string containing sUserData
                *   @param nRunId a user defined identifier that will be passed to pCallback when it is called
		        *   @param sUserData user defined string data
                *   @param connectionTimeout the amount of time to wait for a connection to the server to be accepted before giving up
                *   @param processTimeout the amount of time to wait for the whole connect, send, receive procedure to take place. If set to 0 then the timeout is ignored
                *   @param delay the amount of time to wait before running the function. This wait time may be longer depending on thread allocation.
                *   @return <i>clientResponse</i> a clientResponse object containing the HTTP repsonse code and any data sent from the server
                **/
                void Run(const std::function<void(const clientResponse&, unsigned int, const std::string& )>& pCallback, unsigned int nRunId, const std::string& sUserData, const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0), const std::chrono::milliseconds& delay = std::chrono::milliseconds(0)) const;


                /** @brief The function that attempts to connect to the server, send any data and retrieve any response. This is an asynchronous function
                *   @param pCallback a function to call when the procedure is complete, its arguments are a clientResponse object containing the repsonse from the server and an unsigned int containing the value passed in nRunId
                *   @param nRunId a user defined identifier that will be passed to pCallback when it is called
                *   @param connectionTimeout the amount of time to wait for a connection to the server to be accepted before giving up
                *   @param processTimeout the amount of time to wait for the whole connect, send, receive procedure to take place. If set to 0 then the timeout is ignored
                *   @param delay the amount of time to wait before running the function. This wait time may be longer depending on thread allocation.
                *   @return <i>clientResponse</i> a clientResponse object containing the HTTP repsonse code and any data sent from the server
                **/
                void Run(const std::function<void(const clientResponse&, unsigned int)>& pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0), const std::chrono::milliseconds& delay = std::chrono::milliseconds(0)) const;

                /** @brief Sets a callback function that is called every time a "chunk" of data is sent to the server. This is useful for showing the progress of large uploads
                *   @param pCallback the callback function. It is passed two values: the first is the number of bytes uploaded and the second the total number of bytes that will be uploaded
                **/
                void SetUploadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback)  const;

                /** @brief Sets a callback function that is called every time a "chunk" of data is sent from the server. This is useful for showing the progress of large downloads
                *   @param pCallback the callback function. It is passed two values: the first is the number of bytes downloaded and the second the total number of bytes that will be downloaded
                **/
                void SetDownloadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback) const;

                /** @brief Cancels a running procedure
                **/
                void Cancel() const;

                bool SetBasicAuthentication(const userName& user, const password& pass) const;
                bool SetBearerAuthentication(const std::string& sToken) const;

                /** @brief Set the file location of the certificate authority file to use (if any)
                *   @param ca the file location
                *   @return <i>bool</i> false if already called Run with a callback function
                **/
                bool SetCertificateAuthority(const std::filesystem::path& ca) const;

                /** @brief Sets the client certificate to use if server requires authentication
                *   @param cert the path to the certificate file
                *   @param key the path to the private key file
                *   @return <i>bool</i> false if already called Run with a callback function
                **/
                bool SetClientCertificate(const std::filesystem::path& cert, const std::filesystem::path& key) const;


                /** @brief Set the HTTP method to use
                *   @param method the HTTP method (e.g. restgoose::GET)
                *   @return <i>bool</i> false if already called Run with a callback function
                **/
                bool SetMethod(const httpMethod& method) const;

                /** @brief Set the URL to connect to
                *   @param target the URL endpoint to connect to
                *   @return <i>bool</i> false if already called Run with a callback function
                **/
                bool SetEndpoint(const endpoint& target) const;

                /** @brief Set the data to send (if a POST/PULL/PUT request) in JSON format
                *   @param jsData the data to send
                *   @return <i>bool</i> false if already called Run with a callback function
                **/
                bool SetData(const Json::Value& jsData) const;

                /** @brief Set the data to send (if a POST/PULL/PUT request) in plain/text format
                *   @param data the data to send
                *   @return <i>bool</i> false if already called Run with a callback function
                **/
                bool SetData(const textData& data) const;

                /** @brief Set the file to upload (if a POST/PULL/PUT request)
                *   @param filename the name of the file
                *   @param filepath the location of the file
                *   @return <i>bool</i> false if already called Run with a callback function
                **/
                bool SetFile(const textData& filename, const std::filesystem::path& filepath) const;

                /** @brief Set the multipart data to send (if a POST/PULL/PUT request)
                *   @param vData the data to send
                *   @return <i>bool</i> false if already called Run with a callback funtion
                **/
                bool SetPartData(const std::vector<partData>& vData) const;

                /** @brief Add HTTP headers to send
                *   @param mHeaders a map of header name, header value
                *   @return <i>bool</i> false if already called Run with a callback funtion
                **/
                bool AddHeaders(const std::map<headerName, headerValue>& mHeaders) const;

                /** @brief Set the expected format of the response from the server
                *   @param eResponse the expected format
                *   @return <i>bool</i> false if already called Run with a callback funtion
                **/
                bool SetExpectedResponse(const clientResponse::enumResponse eResponse) const;

            private:
                std::shared_ptr<HttpClientImpl> m_pImpl;
        };
    }
}
