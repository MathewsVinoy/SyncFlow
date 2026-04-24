#pragma once

#include <string>

class Logger {
public:
	static void init(const std::string& folder = "log");
	static void info(const std::string& message);
	static void warn(const std::string& message);
	static void error(const std::string& message);
	static void debug(const std::string& message);

private:
	static void write(const std::string& level, const std::string& message);
};
