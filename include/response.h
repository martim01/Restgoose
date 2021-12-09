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
using endpoint = NamedType<std::string, struct endpointParameter>;
using methodpoint = std::pair<httpMethod, endpoint>;
using query = NamedType<std::string, struct QueryParameter>;
using userName = NamedType<std::string, struct userParamater>;
using password = NamedType<std::string, struct passwordParamater>;
using ipAddress = NamedType<std::string, struct ipAddressParamater>;


struct partData
{
    std::string sHeader;
    std::string sName;
    std::string sFilename;
    std::string sData;
};

using postData = std::vector<partData>;

struct RG_EXPORT response
{
    response(unsigned short nCode=200) : nHttpCode(nCode), sContentType("application/json"){}
    response(unsigned short nCode, const std::string& sReason) : nHttpCode(nCode), sContentType("application/json")
    {
        jsonData["success"] = (nCode >= 200 && nCode < 300);
        jsonData["reason"].append(sReason);
        jsonData["code"] = nCode;
    }
    response(const response& aResponse) : nHttpCode(aResponse.nHttpCode), jsonData(aResponse.jsonData), sContentType(aResponse.sContentType), sData(aResponse.sData){};
    response& operator=(const response& aResponse)
    {
        if(this != &aResponse)
        {
            nHttpCode = aResponse.nHttpCode;
            jsonData = aResponse.jsonData;
            sContentType = aResponse.sContentType;
            sData = aResponse.sData;
        }
        return *this;

    }

    unsigned short nHttpCode;
    Json::Value jsonData;
    std::string sContentType;
    std::string sData;
};



