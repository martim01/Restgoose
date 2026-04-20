
#include <iostream>
#include <string>

#include "../include/httpclient.h"

namespace
{
	std::string EnsureHttpScheme(const std::string& address)
	{
		if(address.rfind("http://", 0) == 0 || address.rfind("https://", 0) == 0)
		{
			return address;
		}
		return "http://" + address;
	}
}

int main(int argc, char** argv)
{
	std::string proxyAddress;
	std::string targetAddress;

	if(argc >= 3)
	{
		proxyAddress = argv[1];
		targetAddress = argv[2];
	}
	else
	{
		std::cout << "Proxy IP/address (for example 127.0.0.1:8080): ";
		std::getline(std::cin, proxyAddress);

		std::cout << "Target IP/address (for example 10.0.0.5:8000/path): ";
		std::getline(std::cin, targetAddress);
	}

	if(proxyAddress.empty() || targetAddress.empty())
	{
		std::cerr << "Both proxy and target addresses are required.\n";
		std::cerr << "Usage: " << argv[0] << " <proxy_address> <target_address>\n";
		return 1;
	}

	const std::string proxyUrl = EnsureHttpScheme(proxyAddress);
	const std::string targetUrl = EnsureHttpScheme(targetAddress);

	pml::restgoose::HttpClient client(pml::restgoose::kGet, endpoint(targetUrl));
	client.UseProxy(proxyUrl);

	const pml::restgoose::clientResponse& response = client.Run();

	std::cout << "HTTP code: " << response.nHttpCode << "\n";
	std::cout << response.data.Get() << "\n";

	return 0;
}