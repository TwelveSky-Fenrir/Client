// Asset/FileUtil.h — file utilities.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ts2::asset {

// Reads an entire file into memory. Returns false if open/read fails.
inline bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    size_t rd = n ? std::fread(out.data(), 1, static_cast<size_t>(n), f) : 0;
    std::fclose(f);
    return rd == static_cast<size_t>(n);
}

} // namespace ts2::asset
