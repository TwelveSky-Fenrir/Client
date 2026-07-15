// Asset/Zlib.h — décompression zlib via GXDCompress.dll (fidèle au client).
// Le client charge dynamiquement GXDCompress.dll (zlib 1.2.3) et résout
// compressBound/compress2/uncompress (GXD_DeviceCreate 0x401610). On fait pareil :
// c'est le décodeur de l'enveloppe [rawSize][packedSize][flux zlib] partagée par
// .IMG/.MOTION/.SOBJECT/.MOBJECT/.WM/.WG/.WJ/.bin, etc.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ts2::asset {

class Zlib {
public:
    // Singleton : charge GXDCompress.dll une fois (depuis le répertoire de travail).
    static Zlib& Instance();

    bool Available() const { return uncompress_ != nullptr; }

    // Décompresse `src` (srcLen octets) vers `dst` de capacité `rawSize`.
    // Renvoie true si Z_OK et si la taille produite vaut exactement rawSize.
    bool Inflate(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t rawSize);

    // Idem, allouant le buffer de sortie. Lève AssetError en cas d'échec.
    std::vector<uint8_t> InflateTo(const uint8_t* src, size_t srcLen, size_t rawSize);

    // Décode l'enveloppe standard : [rawSize:u32][packedSize:u32][flux zlib].
    // `headerExtra` = nombre d'octets d'en-tête AVANT les deux u32 (ex. "MOTION3\x01"
    // + 4 = 12 pour MOTION ; 0 pour .WM/.IMG). Renvoie les rawSize octets décodés.
    // GXD_DecompressEntity 0x6A1A30 / Asset_DecompressImg 0x53F5E0 (CONFIRMED)
    // ex-VeryOldClient: ZlibScope.h [tOriginalSize][tCompressSize][zlib] — framing identique bit-à-bit.
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
