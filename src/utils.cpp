#include "utils.h"

#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "log.h"


namespace pml::restgoose
{

std::filesystem::path CreateTmpFileName(const std::filesystem::path& path)
{
    std::stringstream sstr;
    auto tp = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
    sstr << seconds.count();
    sstr << "_" << (std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count()%1000000000);

    auto ret = path;
    ret /= sstr.str();
    return ret;
}

std::vector<std::string> SplitString(const std::string& str, char cSplit, size_t nMax)
{
    
    if(str.find(cSplit) == std::string::npos)
    {
        return {str};
    }
    
    std::vector<std::string> vSplit;
    std::istringstream f(str);
    std::string s;

    while (getline(f, s, cSplit))
    {
        if(s.empty() == false)
        {
            if(nMax == 0 || vSplit.size() < nMax)
            {
                vSplit.push_back(s);
            }
            else
            {
                vSplit[nMax-1] = vSplit[nMax-1]+cSplit+s;
            }
        }
    }
    return vSplit;
}

void SplitString(std::queue<std::string>& qSplit, const std::string& str, char cSplit)
{
    while(qSplit.empty() == false)
    {
        qSplit.pop();
    }

    std::istringstream f(str);
    std::string s;

    while (getline(f, s, cSplit))
    {
        if(s.empty() == false)
        {
            qSplit.push(s);
        }
    }
}


bool CmpNoCase(std::string_view str1, std::string_view str2)
{
    return ((str1.size() == str2.size()) && std::equal(str1.begin(), str1.end(), str2.begin(), [](char c1, char c2)
    {
        return (c1==c2 || toupper(c1)==toupper(c2));
    }));
}

std::string CreatePath(std::string sPath)
{
    if(sPath[sPath.length()-1] != '/' && sPath[sPath.length()-1] != '\\')
    {
        sPath+= '/';
    }
    return sPath;
}

std::string& ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch);}));
    return s;
}
std::string& rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){return !std::isspace(ch); }).base(), s.end());
    return s;
}
std::string& trim(std::string& s)
{
    return ltrim(rtrim(s));
}

std::string ConvertFromJson(const Json::Value& jsValue)
{
    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = "";
    return Json::writeString(builder, jsValue);
}

std::string load_tls(const std::filesystem::path& path)
{
    std::ifstream ifFile;

    //attempt to open the file
    ifFile.open(path,std::ios::in);
    if(!ifFile.is_open())
    {
        pml::log::warning("TLS") << "Could not open " << path;
        return std::string();
    }

    std::stringstream isstr;
    isstr << ifFile.rdbuf();
    ifFile.close();
    return isstr.str();
}
}
