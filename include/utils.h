#pragma once
#include <vector>
#include <queue>
#include <string>
#include <chrono>
#include "response.h"
#include <filesystem>


RG_EXPORT std::vector<std::string> SplitString(const std::string& str, char cSplit, size_t nMax=0);
RG_EXPORT void SplitString(std::queue<std::string>& qSplit, const std::string& str, char cSplit);
RG_EXPORT bool CmpNoCase(std::string_view str1, std::string_view str2);
RG_EXPORT std::string CreatePath(std::string sPath);

RG_EXPORT std::string& ltrim(std::string& s);
RG_EXPORT std::string& rtrim(std::string& s);
RG_EXPORT std::string& trim(std::string& s);
RG_EXPORT std::string ConvertFromJson(const Json::Value& jsValue);

RG_EXPORT std::filesystem::path CreateTmpFileName(const std::filesystem::path& path);
