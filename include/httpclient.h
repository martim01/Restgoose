#pragma once
#include <vector>
#include <functional>
#include <chrono>
#include <memory>
#include "response.h"

namespace pml
{
    namespace restgoose
    {
        class HttpClientImpl;

        class RG_EXPORT HttpClient
        {
            public:
                HttpClient(const httpMethod& method, const endpoint& target, const std::map<headerName, headerValue> mExtraHeaders = {});
                HttpClient(const httpMethod& method, const endpoint& target, const textData& data, const headerValue& contentType = headerValue("text/plain"), const std::map<headerName, headerValue> mExtraHeaders = {});
                HttpClient(const httpMethod& method, const endpoint& target, const textData& filename, const fileLocation& filepath, const headerValue& contentType = headerValue("application/octet-stream"), const std::map<headerName, headerValue> mExtraHeaders = {});
                HttpClient(const httpMethod& method, const endpoint& target, const std::vector<partData>& vData, const std::map<headerName, headerValue> mExtraHeaders = {}); //multipart
                ~HttpClient();

                const clientResponse& Run(const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0));
                void RunAsync(std::function<void(const clientResponse&, unsigned int )> pCallback, unsigned int nRunId, const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000), const std::chrono::milliseconds& processTimeout = std::chrono::milliseconds(0));
                void SetProgressCallback(std::function<void(unsigned long, unsigned long)> pCallback);

                void Cancel();

            private:
                std::shared_ptr<HttpClientImpl> m_pImpl;
        };
    }
}
