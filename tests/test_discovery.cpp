// tests/test_discovery.cpp

#include <syncflow/discovery/discovery.h>
#include <iostream>

using namespace syncflow;

int main() {
    std::cout << "Testing Discovery Module\n";
    
    auto& device_mgr = discovery::DeviceManager::instance();
    std::cout << "DeviceManager created\n";
    
    // Add a test device
    DeviceInfo test_device;
    test_device.id = "test:device:001";
    test_device.name = "TestDevice";
    test_device.hostname = "testhost";
    test_device.ip_address = "192.168.1.100";
    test_device.port = 15948;
    test_device.platform = PlatformType::LINUX;
    test_device.version = "0.1.0";
    
    if (device_mgr.add_device(test_device)) {
        std::cout << "Device added successfully\n";
    }
    
    // Retrieve device
    auto device = device_mgr.get_device(test_device.id);
    if (device) {
        std::cout << "Device retrieved: " << device->get_info().name << "\n";
    }
    
    // List all devices
    auto devices = device_mgr.get_all_devices();
    std::cout << "Total devices: " << devices.size() << "\n";
    
    return 0;
}
