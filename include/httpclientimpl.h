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
                void RunAsync(std::function<void(const clientResponse&, unsigned int)> pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0));

                void SetUploadProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback);
                void SetDownloadProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback);

                void Cancel();


                void HandleConnectEvent(mg_connection* pConnection);
                void HandleWroteEvent(mg_connection* pConnection, int nBytes);
                void HandleMessageEvent(mg_http_message* pReply);
                void HandleChunkEvent(mg_connection* pConnection, mg_http_message* pReply);
                void HandleErrorEvent(const char* error);

                enum {ERROR_SETUP, ERROR_TIMEOUT, ERROR_CONNECTION, ERROR_REPLY, ERROR_FILE_READ, ERROR_FILE_WRITE, USER_CANCELLED};

            private:

                HttpClientImpl();
                HttpClientImpl(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue> mExtraHeaders = {});
                HttpClientImpl(const httpMethod& method, const endpoint& target, const Json::Value& jsData, const std::map<headerName, headerValue> mExtraHeaders = {});
                HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType = headerValue("text/plain"), const std::map<headerName, headerValue> mExtraHeaders = {});
                HttpClientImpl(const httpMethod& method, const endpoint& target, const textData& filename, const fileLocation& filepath, const headerValue& contentType = headerValue("application/octet-stream"), const std::map<headerName, headerValue> mExtraHeaders = {});
                HttpClientImpl(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue> mExtraHeaders = {}); //multipart



                void GetContentHeaders(mg_http_message* pReply);
                void GetResponseCode(mg_http_message* pReply);

                void DoLoop(mg_mgr& mgr);

                void HandleSimpleWroteEvent(mg_connection* pConnection);
                void HandleMultipartWroteEvent(mg_connection* pConnection);

                bool SendFile(mg_connection* pConnection, const fileLocation& filename, bool bOpen);

                unsigned long WorkoutDataSize();
                unsigned long WorkoutFileSize(const fileLocation& filename);
                void SetupRedirect();

                methodpoint m_point;
                headerValue m_contentType;
                std::vector<partData> m_vPostData;
                std::map<headerName, headerValue> m_mHeaders;

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
                std::function<void(unsigned long, unsigned long)> m_pUploadProgressCallback = nullptr;
                std::function<void(unsigned long, unsigned long)> m_pDownloadProgressCallback = nullptr;
                std::function<void(const clientResponse&, unsigned int )> m_pAsyncCallback = nullptr;
        };
    }
}