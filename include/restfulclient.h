#pragma once
#include <atomic>
#include <queue>
#include <string>
#include <thread>
#include <fstream>
#include "response.h"
#include <functional>

struct mg_mgr;
struct mg_connection;
struct mg_http_message;

using headerName = NamedType<std::string, struct headerNameParameter>;
using headerValue = NamedType<std::string, struct headerValueParameter>;



struct clientResponse
{
    clientResponse() : nCode(0), nBytesReceived(0){}

    unsigned short nCode;
    headerValue contentType;
    unsigned long nContentLength;
    unsigned long nBytesReceived;
    bool bBinary;
    std::string sData;

};

class clientMessage
{
    public:
        clientMessage();
        clientMessage(const httpMethod& method, const endpoint& target);
        clientMessage(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType = headerValue("text/plain"), const std::map<headerName, headerValue> mExtraHeaders = {});
        clientMessage(const httpMethod& method, const endpoint& target, const textData& filename, const fileLocation& filepath, const headerValue& contentType = headerValue("application/octet-stream"), const std::map<headerName, headerValue> mExtraHeaders = {});
        clientMessage(const httpMethod& method, const endpoint& target, const postData& vData, const std::map<headerName, headerValue> mExtraHeaders = {}); //multipart

        const clientResponse& Run(const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0));

        void SetProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback);


        void HandleConnectEvent(mg_connection* pConnection);
        void HandleWroteEvent(mg_connection* pConnection, int nBytes);
        void HandleMessageEvent(mg_http_message* pReply);
        void HandleChunkEvent(mg_connection* pConnection, mg_http_message* pReply);
        void HandleErrorEvent(const char* error);

        enum {ERROR_SETUP, ERROR_TIMEOUT, ERROR_CONNECTION, ERROR_REPLY, ERROR_FILE_READ, ERROR_FILE_WRITE};

    private:
        void GetContentHeaders(mg_http_message* pReply);
        void GetResponseCode(mg_http_message* pReply);

        void DoLoop(mg_mgr& mgr);

        void HandleSimpleWroteEvent(mg_connection* pConnection);
        void HandleMultipartWroteEvent(mg_connection* pConnection);

        bool SendFile(mg_connection* pConnection, const fileLocation& filename, bool bOpen);

        unsigned long WorkoutDataSize();
        size_t WorkoutFileSize(const fileLocation& filename);
        void SetupRedirect();

        methodpoint m_point;
        headerValue m_contentType;
        postData m_vPostData;
        std::map<headerName, headerValue> m_mHeaders;

        unsigned long m_nContentLength = 0;
        unsigned long m_nBytesSent = 0;

        enum enumStatus{CONNECTING, CONNECTED, SENDING, RECEIVING, COMPLETE, REDIRECTING};
        enumStatus m_eStatus = CONNECTING;
        std::chrono::milliseconds m_connectionTimeout;
        std::chrono::milliseconds m_processTimeout;

        clientResponse m_response;

        size_t m_nPostPart = 0;

        std::ofstream m_ofs;
        std::ifstream m_ifs;

        std::function<void(unsigned long, unsigned long)> m_pProgressCallback = nullptr;
};

