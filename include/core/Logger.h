#pragma once

#include <string>

class Logger {
public:
	static void init(const std::string& folder = "");
	static void setLevel(const std::string& level);
	static void setSyncDataOnly(bool syncOnly = true);
	static void shutdown();
	static void info(const std::string& message);
	static void warn(const std::string& message);
	static void error(const std::string& message);
	static void debug(const std::string& message);

private:
	static void write(const std::string& level, const std::string& message);
	static bool shouldLog(const std::string& message);
};
