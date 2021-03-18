#pragma once
#include "json/json.h"
#include "namedtype.h"

#ifdef _WIN32
    #ifdef RESTGOOSE_DLL
        #define RG_EXPORT __declspec(dllexport)
    #else
        #define RG_EXPORT __declspec(dllimport)
    #endif //
#else
    #define RG_EXPORT
#endif

using httpMethod = NamedType<std::string, struct MethodParameter>;
using url = NamedType<std::string, struct UrlParameter>;
using endpoint = std::pair<httpMethod, url>;
using query = NamedType<std::string, struct QueryParameter>;
using postData = NamedType<std::string, struct PostDataParameter>;
using userName = NamedType<std::string, struct userParamater>;
using password = NamedType<std::string, struct passwordParamater>;
using ipAddress = NamedType<std::string, struct ipAddressParamater>;


struct RG_EXPORT response
{
    response(unsigned short nCode=200) : nHttpCode(nCode){}
    response(unsigned short nCode, const std::string& sReason) : nHttpCode(nCode)
    {
        jsonData["success"] = (nCode >= 200 && nCode < 300);
        jsonData["reason"].append(sReason);
        jsonData["code"] = nCode;
    }
    response(const response& aResponse) : nHttpCode(aResponse.nHttpCode), jsonData(aResponse.jsonData){};
    response& operator=(const response& aResponse)
    {
        if(this != &aResponse)
        {
            nHttpCode = aResponse.nHttpCode;
            jsonData = aResponse.jsonData;
        }
        return *this;

    }

    unsigned short nHttpCode;
    Json::Value jsonData;
};



