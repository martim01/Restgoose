#include "websocketclient.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    using namespace pml::restgoose;
    if(argc != 5 && argc != 6)
    {
        std::cerr << "Usage: websocket_proxy_client target proxy exclusion ca [second-target]\n";
        return 2;
    }
    std::atomic<int> messages{0}, errors{0}, opened{0};
    {
        WebSocketClient client(
            [&](const endpoint&, bool connected, int code)
            {
                if(connected) ++opened;
                else if(code != 0) ++errors;
                return true;
            },
            [&](const endpoint&, const std::string& message)
            {
                if(message == "echo:payload") ++messages;
                else ++errors;
                return true;
            }, 10, false);
        client.UseProxy(endpoint(argv[2]),
                        std::string(argv[3]).empty() ? std::set<endpoint>{} :
                        std::set<endpoint>{endpoint(argv[3])},
                        {{headerName("Proxy-Authorization"), headerValue("Basic proxy-only")}});
        if(std::string(argv[4]) == "ignore") client.IgnoreSSLErrors(true);
        else if(std::string(argv[4]).empty() == false && !client.SetCertificateAuthority(argv[4])) return 3;
        client.Run();
        std::vector<endpoint> targets{endpoint(argv[1])};
        if(argc == 6) targets.emplace_back(argv[5]);
        for(const auto& target : targets)
        {
            client.Connect(target);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while(!client.Send(target, std::string("payload")) && errors == 0 &&
                  std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(messages < static_cast<int>(targets.size()) && errors == 0 &&
              std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        client.Stop();
    }
    std::cout << "opened=" << opened << " messages=" << messages << " errors=" << errors << "\n";
    return 0;
}
