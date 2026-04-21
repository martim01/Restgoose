
#include <iostream>
#include <string>

#include "log.h"
#include "../include/httpclient.h"

#include "argparse.hpp"

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

	argparse::ArgumentParser program("client");
	program.add_argument("target");
	program.add_argument("-p", "--proxy").help("Proxy").default_value("");
	program.add_argument("-d", "--dns").help("DNS Server").default_value("");
	program.add_argument("-l", "--log").help("Log Level").default_value("0");

	try
    {
        program.parse_args(argc, argv);
    }
    catch(const std::exception& e)
    {
        std::cout << "CAUGHT " << e.what() << std::endl;
        std::cout << program << std::endl;
        std::exit(1);
    }

	targetAddress = program.get<std::string>("target");
	proxyAddress = program.get<std::string>("--proxy");

	
	pml::log::Stream::AddOutput(std::make_unique<pml::log::Output>(pml::log::Output::kTsDate | pml::log::Output::kTsTime, pml::log::Output::TS::kMillisecond));

	std::string proxyUrl;
	if(!proxyAddress.empty()) 
	{
		proxyUrl = EnsureHttpScheme(proxyAddress);
	}

	const std::string targetUrl = EnsureHttpScheme(targetAddress);

	pml::restgoose::HttpClient client(pml::restgoose::kGet, endpoint(targetUrl));
	client.SetDNS(program.get<std::string>("--dns"));

	if(proxyUrl.empty() == false)
	{
		client.UseProxy(proxyUrl);
	}

	if(program.get<std::string>("--log") != "0")
	{
		try
		{
			auto sLevel = program.get<std::string>("--log");
			unsigned int nLevel = std::stoul(sLevel);
			client.SetDebugLogLevel(nLevel);
		}
		catch(const std::exception& e)
		{
			std::cout << "Invalid log level: " << e.what() << std::endl;
			std::exit(1);
		}
	}


	const pml::restgoose::clientResponse& response = client.Run();

	pml::log::info() << "HTTP code: " << response.nHttpCode << "\n";
	pml::log::info() << response.data.Get() << "\n";

	pml::log::Stream::Stop();
	return 0;
}