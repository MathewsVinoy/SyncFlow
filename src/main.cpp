
#include "core/Application.h"

int main() {
	Application app;
	if (!app.init()) {
		app.shutdown();
		return 1;
	}

	const int exitCode = app.run();
	app.shutdown();
	return exitCode;
}

