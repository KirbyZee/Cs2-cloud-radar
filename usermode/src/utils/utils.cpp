#include "pch.hpp"

std::string utils::get_ipv4_address(config_data_t& config_data)
{
	if (config_data.m_use_localhost)
		return "localhost";

	std::string ip_address;
	std::array<char, 128> buffer{};

	const auto pipe = _popen("ipconfig", "r");
	if (!pipe)
	{
		LOG_ERROR("failed to open a pipe to ipconfig");
		return {};
	}

	while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
	{
		std::string line(buffer.data());
		std::smatch match{};
		if (regex_search(line, match, std::regex(R"((192\.168\.\d+\.\d+))")) && match.size() == 2)
		{
			ip_address = match[1];
			break;
		}
	}
	_pclose(pipe);

	return ip_address;
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp)
{
    userp->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

