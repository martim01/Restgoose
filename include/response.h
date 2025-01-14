#pragma once
#include "namedtype.h"
#include <vector>
#include "json/json.h"
#include "dllexport.h"


using httpMethod = NamedType<std::string, struct MethodParameter>;
using endpoint = NamedType<std::string, struct endpointParameter>;
using methodpoint = std::pair<httpMethod, endpoint>;
using userName = NamedType<std::string, struct userParamater>;
using password = NamedType<std::string, struct passwordParamater>;
using ipAddress = NamedType<std::string, struct ipAddressParamater>;
using headerName = NamedType<std::string, struct headerNameParameter>;
using headerValue = NamedType<std::string, struct headerValueParameter>;

using textData = NamedType<std::string, struct textDataParameter>;
using fileLocation = NamedType<std::string, struct fileLocationParameter>;
using partName = NamedType<std::string, struct partNameParameter>;
using queryKey = NamedType<std::string, struct queryKeyParameter>;
using queryValue = NamedType<std::string, struct queryValueParameter>;



struct query_less
{
    bool operator() (queryKey e1, queryKey e2) const;
};

using query = std::map<queryKey, queryValue, query_less>;




namespace pml
{
    namespace restgoose
    {
        extern RG_EXPORT const httpMethod GET;
        extern RG_EXPORT const httpMethod POST;
        extern RG_EXPORT const httpMethod PUT;
        extern RG_EXPORT const httpMethod PATCH;
        extern RG_EXPORT const httpMethod HTTP_DELETE;
        extern RG_EXPORT const httpMethod OPTIONS;

        struct RG_EXPORT partData
        {
            friend class HttpClientImpl;
            friend class MongooseServer;

            partData(){}
            partData(const partName& n, const textData& d) : name(n), data(d){}
            partData(const partName& n, const textData& filename, const fileLocation& filelocation) : name(n), data(filename), filepath(filelocation){}


            partName name;
            textData data;
            fileLocation filepath;

            private:
                std::string sHeader;
        };



        struct RG_EXPORT response
        {
            response(unsigned short nCode=200);
            response(unsigned short nCode, const std::string& sReason);
            response(const response& aResponse);
            response& operator=(const response& aResponse);
            ~response();
            unsigned short nHttpCode;
            Json::Value jsonData;
            headerValue contentType;
            textData data;
            bool bFile;
        };

        struct RG_EXPORT clientResponse
        {
            //clientResponse() : nHttpCode(0), nBytesReceived(0){}

            unsigned short nHttpCode=500;
            headerValue contentType;
            unsigned long nContentLength=0;
            unsigned long nBytesReceived=0;
            bool bBinary=false;
            textData data;
            std::multimap<headerName, headerValue> mmHeaders;

            enum enumResponse {TEXT, FILE, AUTO};
            enum enumError {ERROR_SETUP, ERROR_TIME, ERROR_CONNECTION, ERROR_REPLY, ERROR_FILE_READ, ERROR_FILE_WRITE, USER_CANCELLED};

        };
    };
};


