// Asset/ByteReader.h — bounded little-endian read cursor.
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ts2::asset {

class AssetError : public std::runtime_error {
public:
    explicit AssetError(const std::string& m) : std::runtime_error(m) {}
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : data_(v.data()), size_(v.size()) {}

    size_t Pos() const { return pos_; }
    size_t Size() const { return size_; }
    size_t Remaining() const { return size_ - pos_; }
    bool   Eof() const { return pos_ >= size_; }
    const uint8_t* Ptr() const { return data_ + pos_; }

    void Seek(size_t p) { if (p > size_) throw AssetError("Seek hors limites"); pos_ = p; }
    void Skip(size_t n) { Require(n); pos_ += n; }

    uint8_t  U8()  { Require(1); return data_[pos_++]; }
    uint16_t U16() { Require(2); uint16_t v; std::memcpy(&v, data_ + pos_, 2); pos_ += 2; return v; }
    uint32_t U32() { Require(4); uint32_t v; std::memcpy(&v, data_ + pos_, 4); pos_ += 4; return v; }
    int32_t  I32() { return static_cast<int32_t>(U32()); }
    uint64_t U64() { Require(8); uint64_t v; std::memcpy(&v, data_ + pos_, 8); pos_ += 8; return v; }
    float    F32() { uint32_t u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    double   F64() { uint64_t u = U64(); double d; std::memcpy(&d, &u, 8); return d; }

    void Read(void* dst, size_t n) { Require(n); std::memcpy(dst, data_ + pos_, n); pos_ += n; }

    // Reads n bytes as a string (not null-terminated in the source).
    std::string Str(size_t n) {
        Require(n);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

    // Compares the current 'n' bytes to 'sig' without advancing.
    bool PeekMagic(const char* sig, size_t n) const {
        if (pos_ + n > size_) return false;
        return std::memcmp(data_ + pos_, sig, n) == 0;
    }

private:
    void Require(size_t n) const {
        if (pos_ + n > size_) throw AssetError("Lecture hors limites");
    }
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

} // namespace ts2::asset
