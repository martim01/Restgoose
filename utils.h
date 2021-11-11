#pragma once
#include <vector>
#include <queue>
#include <string>
#include <chrono>
#include "response.h"


RG_EXPORT std::vector<std::string> SplitString(std::string str, char cSplit, size_t nMax=0);
RG_EXPORT void SplitString(std::queue<std::string>& qSplit, std::string str, char cSplit);
RG_EXPORT bool CmpNoCase(const std::string& str1, const std::string& str2);
RG_EXPORT std::string CreatePath(std::string sPath);
RG_EXPORT std::string GetCurrentTimeAsString(bool bIncludeNano);
RG_EXPORT std::string GetCurrentTimeAsIsoString();
RG_EXPORT std::string ConvertTimeToString(std::chrono::time_point<std::chrono::system_clock> tp, bool bIncludeNano);
RG_EXPORT std::string ConvertTimeToIsoString(std::chrono::time_point<std::chrono::system_clock> tp);
RG_EXPORT std::string ConvertTimeToIsoString(std::time_t t);
RG_EXPORT std::string& ltrim(std::string& s);
RG_EXPORT std::string& rtrim(std::string& s);
RG_EXPORT std::string& trim(std::string& s);
RG_EXPORT std::string ConvertFromJson(const Json::Value& jsValue);
