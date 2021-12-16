#include "response.h"

#include "json/json.h"

response::response(unsigned short nCode, const std::string& sReason) : nHttpCode(nCode), jsonData(Json::objectValue), sContentType("application/json")
{
    jsonData["success"] = (nCode >= 200 && nCode < 300);
    jsonData["reason"].append(sReason);
    jsonData["code"] = nCode;
}

response::response(unsigned short nCode) : nHttpCode(nCode), jsonData(Json::objectValue), sContentType("application/json")
{}

response::response(const response& aResponse) : nHttpCode(aResponse.nHttpCode), jsonData(aResponse.jsonData), sContentType(aResponse.sContentType), sData(aResponse.sData){};

response& response::operator=(const response& aResponse)
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


