#include "response.h"
#include "json/json.h"

using namespace pml::restgoose;


bool query_less::operator() (queryKey e1, queryKey e2) const
{
    return std::lexicographical_compare(e1.Get().begin(), e1.Get().end(), e2.Get().begin(), e2.Get().end(), [](unsigned char a, unsigned char b){
                                        return toupper(a) < toupper(b);
                                        });
}

const httpMethod pml::restgoose::GET    = httpMethod("GET");
const httpMethod pml::restgoose::POST   = httpMethod("POST");
const httpMethod pml::restgoose::PUT    = httpMethod("PUT");
const httpMethod pml::restgoose::PATCH  = httpMethod("PATCH");
const httpMethod pml::restgoose::HTTP_DELETE = httpMethod("DELETE");
const httpMethod pml::restgoose::OPTIONS = httpMethod("OPTIONS");


response::response(unsigned short nCode, const std::string& sReason) : nHttpCode(nCode), jsonData(Json::objectValue), contentType("application/json"), bFile(false)
{
    jsonData["success"] = (nCode >= 200 && nCode < 300);
    jsonData["reason"].append(sReason);
    jsonData["code"] = nCode;
}

response::response(unsigned short nCode) : nHttpCode(nCode), contentType("application/json"), bFile(false)
{}

response::response(unsigned short nCode, const Json::Value& jsData) : nHttpCode(nCode), jsonData(jsData), contentType("application/json"), bFile(false){}

response::response(const response& aResponse) : nHttpCode(aResponse.nHttpCode),
jsonData(aResponse.jsonData),
contentType(aResponse.contentType),
data(aResponse.data),
bFile(aResponse.bFile),
mHeaders(aResponse.mHeaders),
mExtraHeaders(aResponse.mExtraHeaders)
{
}


response& response::operator=(const response& aResponse)
{
    if(this != &aResponse)
    {
        nHttpCode = aResponse.nHttpCode;
        jsonData = aResponse.jsonData;
        contentType = aResponse.contentType;
        data = aResponse.data;
        bFile = aResponse.bFile;
        mHeaders = aResponse.mHeaders;
        mExtraHeaders = aResponse.mExtraHeaders;
    }
    return *this;

}


response::~response()=default;
