// Asset/Zlib.h — zlib decompression via GXDCompress.dll (faithful to the client).
// The client dynamically loads GXDCompress.dll (zlib 1.2.3) and resolves
// compressBound/compress2/uncompress (GXD_DeviceCreate 0x401610). We do the same:
// this is the decoder for the [rawSize][packedSize][zlib stream] envelope shared
// by .IMG/.MOTION/.SOBJECT/.MOBJECT/.WM/.WG/.WJ/.bin, etc.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ts2::asset {

class Zlib {
public:
    // Singleton: loads GXDCompress.dll once (from the working directory).
    static Zlib& Instance();

    bool Available() const { return uncompress_ != nullptr; }

    // Decompresses `src` (srcLen bytes) into `dst` of capacity `rawSize`.
    // Returns true if Z_OK and the produced size is exactly rawSize.
    bool Inflate(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t rawSize);

    // Same, allocating the output buffer. Throws AssetError on failure.
    std::vector<uint8_t> InflateTo(const uint8_t* src, size_t srcLen, size_t rawSize);

    // Decodes the standard envelope: [rawSize:u32][packedSize:u32][zlib stream].
    // `headerExtra` = number of header bytes BEFORE the two u32 (e.g. "MOTION3\x01"
    // + 4 = 12 for MOTION; 0 for .WM/.IMG). Returns the rawSize decoded bytes.
    // GXD_DecompressEntity 0x6A1A30 / Asset_DecompressImg 0x53F5E0 (CONFIRMED)
    // ex-VeryOldClient: ZlibScope.h [tOriginalSize][tCompressSize][zlib] — bit-for-bit identical framing.
    std::vector<uint8_t> DecodeEnvelope(const uint8_t* data, size_t size,
                                        size_t headerExtra = 0);

private:
    Zlib();
    ~Zlib();
    Zlib(const Zlib&) = delete;
    Zlib& operator=(const Zlib&) = delete;

    void* dll_ = nullptr; // HMODULE (GXDCompress.dll)
    // int uncompress(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen)
    using UncompressFn = int(__cdecl*)(uint8_t*, unsigned long*, const uint8_t*, unsigned long);
    UncompressFn uncompress_ = nullptr;
};

} // namespace ts2::asset
