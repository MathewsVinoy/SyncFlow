#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include "syncflow/networking/peer_node.h"
#include "syncflow/platform/system_info.h"

#include <cstring>

int main(int argc, char** argv) {
    std::string device_name = syncflow::platform::get_hostname();
    if (argc > 1 && argv[1] != nullptr && std::strlen(argv[1]) > 0) {
        device_name = argv[1];
    }

    syncflow::networking::PeerNode node(device_name);
    node.run();
    return 0;
}
	g_running = false;
