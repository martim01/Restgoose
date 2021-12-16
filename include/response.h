#pragma once
#include "namedtype.h"
#include <vector>
#include "json/json.h"

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

using textData = NamedType<std::string, struct textDataParameter>;
using fileLocation = NamedType<std::string, struct fileLocationParameter>;
using partName = NamedType<std::string, struct partNameParameter>;

struct partData
{
    partData(){}
    partData(const partName& n, const textData& d) : name(n), data(d){}
    partData(const partName& n, const textData& filename, const fileLocation& filelocation) : name(n), data(filename), filepath(filelocation){}

    std::string sHeader;
    partName name;
    textData data;
    fileLocation filepath;

};

using postData = std::vector<partData>;


struct RG_EXPORT response
{
    response(unsigned short nCode=200);
    response(unsigned short nCode, const std::string& sReason);

    response(const response& aResponse);
    response& operator=(const response& aResponse);
    unsigned short nHttpCode;
    Json::Value jsonData;
    std::string sContentType;
    std::string sData;
};



