#pragma once
#include <CommonCrypto/CommonDigest.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace lvt {
inline std::string sha256File(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Model file not found");
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    std::array<char, 65536> buffer{};
    while (stream) {
        stream.read(buffer.data(), buffer.size());
        if (stream.gcount()) CC_SHA256_Update(&context, buffer.data(), CC_LONG(stream.gcount()));
    }
    if (!stream.eof()) throw std::runtime_error("Could not read model file");
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    CC_SHA256_Final(digest.data(), &context);
    std::ostringstream result;
    for (auto byte : digest) result << std::hex << std::setfill('0') << std::setw(2) << int(byte);
    return result.str();
}
inline void verifyModelHash(const std::string& path, const std::string& expected) {
    if (expected.size() != 64 || sha256File(path) != expected) throw std::runtime_error("Model SHA-256 verification failed");
}
}
