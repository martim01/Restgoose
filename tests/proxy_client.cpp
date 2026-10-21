#include "httpclient.h"
#include "rgutils.h"

#include <iostream>

int main(int argc, char** argv)
{
    using namespace pml::restgoose;
    if(argc != 6)
    {
        std::cerr << "Usage: proxy_client target proxy exclusion ca body\n";
        return 2;
    }
    const endpoint proxy(argv[2]);
    const endpoint testProxy("http://127.0.0.1:1");
    if(is_proxy_needed(endpoint("https://example.com/api"), testProxy, {endpoint("example.com")}) ||
       is_proxy_needed(endpoint("https://example.com/api"), testProxy, {endpoint("https://example.com/#")}) ||
       is_proxy_needed(endpoint("https://sub.example.com/api"), testProxy, {endpoint("*.example.com")}) ||
       is_proxy_needed(endpoint("https://example.com/api"), endpoint(""), {}) ||
       !is_proxy_needed(endpoint("https://other.example/api"), testProxy, {endpoint("example.com")}))
    {
        std::cerr << "Proxy exclusion matching failed\n";
        return 3;
    }
    const std::string body(argv[5]);
    HttpClient client(body.empty() ? kGet : kPost, endpoint(argv[1]), textData(body),
                      headerValue("text/plain"),
                      {{headerName("Authorization"), headerValue("Bearer origin-only")},
                       {headerName("pRoXy-AuThOrIzAtIoN"), headerValue("Basic proxy-only")}});
    client.UseProxy(proxy, std::string(argv[3]).empty() ? std::set<endpoint>{} :
                    std::set<endpoint>{endpoint(argv[3])});
    if(std::string(argv[4]) == "ignore")
    {
        client.IgnoreSSLErrors(true);
    }
    else if(std::string(argv[4]).empty() == false && !client.SetCertificateAuthority(argv[4]))
    {
        std::cerr << "Could not load test CA\n";
        return 4;
    }
    const auto& response = client.Run(std::chrono::milliseconds(2000), std::chrono::milliseconds(4000));
    if(response.mHeaders.count(headerName("X-Old")) != 0)
    {
        std::cerr << "Response headers leaked across redirects\n";
        return 5;
    }
    std::cout << response.nHttpCode << "\n" << response.data.Get() << "\n";
}
