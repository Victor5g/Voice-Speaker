#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace lvt {
// Strict offline fixture reader: little-endian PCM16 mono at 16 kHz.
inline std::vector<float> readWav16k(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 44 || file.tellg() > 16000 * 2 * 600 + 65536) throw std::runtime_error("Invalid or oversized WAV");
    std::vector<unsigned char> data(std::size_t(file.tellg()));
    file.seekg(0); file.read(reinterpret_cast<char *>(data.data()), data.size());
    auto u16 = [&](std::size_t p) { return unsigned(data[p]) | (unsigned(data[p + 1]) << 8); };
    auto u32 = [&](std::size_t p) { return std::uint32_t(data[p]) | (std::uint32_t(data[p + 1]) << 8) | (std::uint32_t(data[p + 2]) << 16) | (std::uint32_t(data[p + 3]) << 24); };
    if (std::memcmp(data.data(), "RIFF", 4) || std::memcmp(data.data() + 8, "WAVE", 4)) throw std::runtime_error("Not a RIFF WAV");
    bool validFormat = false;
    std::size_t audioOffset = 0, audioBytes = 0;
    for (std::size_t p = 12; p + 8 <= data.size();) {
        const auto size = u32(p + 4);
        if (size > data.size() - p - 8) throw std::runtime_error("Truncated WAV chunk");
        if (!std::memcmp(data.data() + p, "fmt ", 4) && size >= 16)
            validFormat = u16(p + 8) == 1 && u16(p + 10) == 1 && u32(p + 12) == 16000 && u16(p + 22) == 16;
        if (!std::memcmp(data.data() + p, "data", 4)) { audioOffset = p + 8; audioBytes = size; }
        p += 8 + size + (size & 1);
    }
    if (!validFormat || !audioOffset || audioBytes % 2) throw std::runtime_error("Expected mono PCM16 16000 Hz WAV");
    std::vector<float> samples(audioBytes / 2);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = std::int16_t(u16(audioOffset + 2 * i)) / 32768.0f;
    return samples;
}
}
