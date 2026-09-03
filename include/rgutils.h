#ifndef PML_RESTGOOSE_UTILS
#define PML_RESTGOOSE_UTILS

#include <filesystem>
#include <chrono>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "response.h"


namespace pml::restgoose
{
    RG_EXPORT extern std::vector<std::string> split_string(const std::string& str, char cSplit, size_t nMax=0);
    //RG_EXPORT extern void split_string(std::queue<std::string>& qSplit, const std::string& str, char cSplit);
    RG_EXPORT extern bool cmp_no_case(std::string_view str1, std::string_view str2);
    RG_EXPORT extern std::string create_path(std::string sPath);

    RG_EXPORT extern std::string& ltrim(std::string& s);
    RG_EXPORT extern std::string& rtrim(std::string& s);
    RG_EXPORT extern std::string& trim(std::string& s);
    RG_EXPORT extern std::string convert_from_json(const Json::Value& jsValue);

    RG_EXPORT extern std::filesystem::path create_tmp_file_name(const std::filesystem::path& path);

    extern std::string load_tls(const std::filesystem::path& path);

    RG_EXPORT extern std::optional<Json::Value> convert_to_json(const std::string& str);

    RG_EXPORT extern std::optional<Json::Value> convert_post_to_json(const std::vector<partData>& vData);
}

#endif