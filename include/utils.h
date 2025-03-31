#ifndef PML_RESTGOOSE_UTILS
#define PML_RESTGOOSE_UTILS

#include <filesystem>
#include <chrono>
#include <queue>
#include <string>
#include <vector>

#include "response.h"


namespace pml::restgoose
{
    
    RG_EXPORT extern std::vector<std::string> SplitString(const std::string& str, char cSplit, size_t nMax=0);
    RG_EXPORT extern void SplitString(std::queue<std::string>& qSplit, const std::string& str, char cSplit);
    RG_EXPORT extern bool CmpNoCase(std::string_view str1, std::string_view str2);
    RG_EXPORT extern std::string CreatePath(std::string sPath);

    RG_EXPORT extern std::string& ltrim(std::string& s);
    RG_EXPORT extern std::string& rtrim(std::string& s);
    RG_EXPORT extern std::string& trim(std::string& s);
    RG_EXPORT extern std::string ConvertFromJson(const Json::Value& jsValue);

    RG_EXPORT extern std::filesystem::path CreateTmpFileName(const std::filesystem::path& path);
}

#endif