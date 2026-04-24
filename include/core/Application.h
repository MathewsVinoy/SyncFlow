#pragma once

#include "core/ConfigManager.h"

class Application {
public:
	bool init();
	int run();
	void shutdown();

private:
	ConfigManager config_;
	bool initialized_ = false;
};
