#pragma once
#include <atomic>
#include <queue>
#include <string>
#include <thread>
#include <fstream>
#include "response.h"
#include <functional>
#include <atomic>
#include "json/json.h"
#include <filesystem>

struct mg_mgr;
struct mg_connection;
struct mg_http_message;


namespace pml
{
    namespace restgoose
    {
        class HttpClientImpl
        {
            friend class HttpClient;

            public:
                ~HttpClientImpl();

                const clientResponse& Run(const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0));
                void RunAsync(const std::function<void(const clientResponse&, unsigned int, const std::string&)>& pCallback, unsigned int nRunId, const std::string& sUserData, const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0));

                void RunAsyncOld(const std::function<void(const clientResponse&, unsigned int)>& pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0));

                void SetUploadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback);
                void SetDownloadProgressCallback(const std::function<void(unsigned long, unsigned long)>& pCallback);

                void Cancel();


                void HandleConnectEvent(mg_connection* pConnection);
                void HandleConnectEventDirect(mg_connection* pConnection);
               void HandleConnectEventToProxy(mg_connection* pConnection);
                void HandleReadEvent(mg_connection* pConnection);
                void HandleWroteEvent(mg_connection* pConnection, int nBytes);
                void HandleMessageEvent(mg_http_message* pReply);
                void HandleChunkEvent(mg_connection* pConnection, mg_http_message* pReply);
                void HandleErrorEvent(const char* error);

                bool SetBasicAuthentication(const userName& user, const password& pass);
                bool SetBearerAuthentication(const std::string& sToken);

                void UseProxy(const endpoint& proxy);

            private:

                HttpClientImpl();
                HttpClientImpl(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);
                HttpClientImpl(const httpMethod& method, const endpoint& target, const Json::Value& jsData, const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);
                HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType = headerValue("text/plain"), const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);
                HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& filename, const std::filesystem::path& filepath, const headerValue& contentType = headerValue("application/octet-stream"), const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO);
                HttpClientImpl(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue>& mExtraHeaders = {}, clientResponse::enumResponse eResponse=clientResponse::enumResponse::AUTO); //multipart

                bool SetCertificateAuthority(const std::filesystem::path& ca);
                bool SetClientCertificate(const std::filesystem::path& cert, const std::filesystem::path& key);

                bool SetMethod(const httpMethod& method);
                bool SetEndpoint(const endpoint& target);
                bool SetData(const Json::Value& jsData);
                bool SetData(const textData& data);
                bool SetFile(const textData& filename, const std::filesystem::path& filepath);
                bool SetPartData(const std::vector<partData>& vData);
                bool AddHeaders(const std::map<headerName, headerValue>& mHeaders);
                bool SetExpectedResponse(const clientResponse::enumResponse eResponse);



                void GetContentHeaders(mg_http_message* pReply);
                void GetResponseCode(mg_http_message* pReply);

                void DoLoop(mg_mgr& mgr) const;

                void HandleSimpleWroteEvent(mg_connection* pConnection);
                void HandleMultipartWroteEvent(mg_connection* pConnection);

                bool SendFile(mg_connection* pConnection, const std::filesystem::path& filename, bool bOpen);

                unsigned long WorkoutDataSize();
                unsigned long WorkoutFileSize(const std::filesystem::path& filename);
                void SetupRedirect();

                methodpoint m_point = methodpoint(GET, endpoint("127.0.0.1"));
                headerValue m_contentType = headerValue("text/plain");

                std::vector<partData> m_vPostData;
                std::map<headerName, headerValue> m_mHeaders;
                clientResponse::enumResponse m_eResponse = clientResponse::enumResponse::AUTO;

                unsigned long m_nContentLength = 0;
                unsigned long m_nBytesSent = 0;

                enum enumStatus{CONNECTING, CONNECTED, SENDING, RECEIVING, COMPLETE, REDIRECTING};
                std::atomic<enumStatus> m_eStatus{enumStatus::CONNECTING};
                std::chrono::milliseconds m_connectionTimeout;
                std::chrono::milliseconds m_processTimeout;

                clientResponse m_response;

                size_t m_nPostPart = 0;

                std::ofstream m_ofs;
                std::ifstream m_ifs;
                unsigned int m_nRunId = 0;
	            std::string m_sUserData;

                std::filesystem::path m_ca;
                std::filesystem::path m_Cert;
                std::filesystem::path m_Key;

                std::function<void(unsigned long, unsigned long)> m_pUploadProgressCallback = nullptr;
                std::function<void(unsigned long, unsigned long)> m_pDownloadProgressCallback = nullptr;
                std::function<void(const clientResponse&, unsigned int, const std::string& )> m_pAsyncCallback = nullptr;
                std::function<void(const clientResponse&, unsigned int)> m_pAsyncCallbackV1 = nullptr;
				endpoint m_proxy;
                bool m_bConnectedViaProxy{false};
        };
    }
}
