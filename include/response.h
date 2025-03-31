#ifndef PML_RESTGOOSE_RESPONSE
#define PML_RESTGOOSE_RESPONSE

#include <vector>
#include <filesystem>

#include "json/json.h"

#include "dllexport.h"
#include "namedtype.h"

using httpMethod = NamedType<std::string, struct MethodParameter>;
using endpoint = NamedType<std::string, struct endpointParameter>;
using methodpoint = std::pair<httpMethod, endpoint>;
using userName = NamedType<std::string, struct userParamater>;
using password = NamedType<std::string, struct passwordParamater>;
using ipAddress = NamedType<std::string, struct ipAddressParamater>;
using headerName = NamedType<std::string, struct headerNameParameter>;
using headerValue = NamedType<std::string, struct headerValueParameter>;

using textData = NamedType<std::string, struct textDataParameter>;
using partName = NamedType<std::string, struct partNameParameter>;
using queryKey = NamedType<std::string, struct queryKeyParameter>;
using queryValue = NamedType<std::string, struct queryValueParameter>;



struct query_less
{
    bool operator() (queryKey e1, queryKey e2) const;
};

using query = std::map<queryKey, queryValue, query_less>;




namespace pml::restgoose
{
    extern RG_EXPORT const httpMethod kGet;
    extern RG_EXPORT const httpMethod kPost;
    extern RG_EXPORT const httpMethod kPut;
    extern RG_EXPORT const httpMethod kPatch;
    extern RG_EXPORT const httpMethod kDelete;
    extern RG_EXPORT const httpMethod kOptions;

    struct RG_EXPORT partData
    {
        friend class HttpClientImpl;
        friend class MongooseServer;

        partData()=default;
        partData(const partName& n, const textData& d) : name(n), data(d){}
        partData(const partName& n, const textData& filename, const std::filesystem::path& filelocation) : name(n), data(filename), filepath(filelocation){}


        partName name;
        textData data;
        std::filesystem::path filepath;

        private:
            std::string sHeader;
    };



    struct RG_EXPORT response
    {
        explicit response(unsigned short nCode=200);
        response(unsigned short nCode, const std::string& sReason);
        response(unsigned short nCode, const Json::Value& jsData);
        response(const response& aResponse);
        response& operator=(const response& aResponse);
        ~response();

        unsigned short nHttpCode;                           /** The HTTP status code to send back **/
        Json::Value jsonData;                               /** Any Json encode data to send back **/
        headerValue contentType;                            /** The content type that is being sent back **/
        textData data;                                      /** Any plain text data to send back **/
        bool bFile;                                         /** If true then a file is being sent back and data is the location of the file **/
        std::map<headerName, headerValue> mHeaders;         /** overides the default headers sent by the server **/
        std::map<headerName, headerValue> mExtraHeaders;    /** extra headers to send as well as the default headers **/
    };

    struct RG_EXPORT clientResponse
    {
        unsigned short nHttpCode=500;
        headerValue contentType;
        unsigned long nContentLength=0;
        unsigned long nBytesReceived=0;
        bool bBinary=false;
        textData data;
        std::map<headerName, headerValue> mHeaders;

        enum enumResponse {kText, kFile, kAuto};
        enum enumError {kErrorSetup, kErrorTime, kErrorConnection, kErrorReply, kErrorFileRead, kErrorFileWrite, kUserCancelled};

    };
}


#endif