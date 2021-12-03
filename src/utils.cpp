#include <sstream>
#include <iostream>
#include <iomanip>
#include <string.h>
#include "utils.h"
#include <algorithm>


using namespace std;


vector<string> SplitString(string str, char cSplit, size_t nMax)
{
    vector<string> vSplit;
    istringstream f(str);
    string s;

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

void SplitString(queue<string>& qSplit, string str, char cSplit)
{
    while(qSplit.empty() == false)
    {
        qSplit.pop();
    }

    istringstream f(str);
    string s;

    while (getline(f, s, cSplit))
    {
        if(s.empty() == false)
        {
            qSplit.push(s);
        }
    }
}


bool CmpNoCase(const string& str1, const string& str2)
{
    return ((str1.size() == str2.size()) && equal(str1.begin(), str1.end(), str2.begin(), [](char c1, char c2)
    {
        return (c1==c2 || toupper(c1)==toupper(c2));
    }));
}

string CreatePath(string sPath)
{
    if(sPath[sPath.length()-1] != '/' && sPath[sPath.length()-1] != '\\')
    {
        sPath+= '/';
    }
    return sPath;
}



std::string GetCurrentTimeAsString(bool bIncludeNano)
{
    std::chrono::time_point<std::chrono::system_clock> tp(std::chrono::system_clock::now());
    return ConvertTimeToString(tp, bIncludeNano);
}

std::string GetCurrentTimeAsIsoString()
{
    std::chrono::time_point<std::chrono::system_clock> tp(std::chrono::system_clock::now());
    return ConvertTimeToIsoString(tp);
}

std::string ConvertTimeToString(std::chrono::time_point<std::chrono::system_clock> tp, bool bIncludeNano)
{
    std::stringstream sstr;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
    sstr << seconds.count();
    if(bIncludeNano)
    {
        sstr << ":" << (std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count()%1000000000);
    }
    return sstr.str();
}

std::string ConvertTimeToIsoString(std::chrono::time_point<std::chrono::system_clock> tp)
{
    std::time_t  t = std::chrono::system_clock::to_time_t(tp);
    return ConvertTimeToIsoString(t);
}

std::string ConvertTimeToIsoString(std::time_t t)
{
    std::stringstream ss;

    ss << std::put_time(std::localtime(&t), "%FT%T%z");
    return ss.str();
}






std::string& ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int,int>(std::isspace))));
    return s;
}
std::string& rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int,int>(std::isspace))).base(), s.end());
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
    //std::stringstream ssJson;
    //ssJson << jsValue;
    //return ssJson.str();
}
