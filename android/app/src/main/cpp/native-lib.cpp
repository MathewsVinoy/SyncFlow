#include <jni.h>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/system_properties.h>
#include <array>

namespace {

std::string read_system_property(const char* key) {
    std::array<char, PROP_VALUE_MAX> value{};
    if (__system_property_get(key, value.data()) > 0) {
        return value.data();
    }
    return {};
}

std::string resolve_mobile_device_name() {
    const std::string model = read_system_property("ro.product.model");
    const std::string device = read_system_property("ro.product.device");
    if (!model.empty() && !device.empty() && model != device) {
        return model + " (" + device + ")";
    }
    if (!model.empty()) {
        return model;
    }
    if (!device.empty()) {
        return device;
    }

    std::array<char, 128> host{};
    if (::gethostname(host.data(), host.size()) == 0 && host[0] != '\0') {
        return host.data();
    }

    return "android-device";
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_syncflow_MainActivity_getPeerStatus(
        JNIEnv* env,
        jobject /* this */) {
    try {
        const std::string device_name = resolve_mobile_device_name();
        const std::string json = std::string("{\"running\": true, \"device_name\": \"") +
                                 json_escape(device_name) +
                                 "\", \"local_ip\": \"\", \"config_path\": \"\", \"connections\": []}";
        return env->NewStringUTF(json.c_str());
    } catch (...) {
        const char* err = "{\"error\":\"native_exception\"}";
        return env->NewStringUTF(err);
    }
}