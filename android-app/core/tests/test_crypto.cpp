#include "CryptoUtils.hpp"
#include <cassert>
#include <iostream>

void test_sha256() {
    std::string data = "hello syncflow";
    std::string hash = syncflow::crypto::sha256(data);
    assert(!hash.empty());
    std::cout << "SHA256 test passed. Hash: " << hash << std::endl;
}

// In a real project we would use a test framework like Catch2 or GTest
// For this skeleton, we'll just use simple asserts
int main_crypto_test() {
    test_sha256();
    return 0;
}
