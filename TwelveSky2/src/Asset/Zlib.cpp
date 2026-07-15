// Asset/Zlib.cpp — backend GXDCompress.dll (zlib 1.2.3).
#include "Asset/Zlib.h"
#include "Asset/ByteReader.h"
#include "Core/Log.h"
#include <windows.h>
#include <cstring>

namespace ts2::asset {

Zlib& Zlib::Instance() {
    static Zlib inst;
    return inst;
}

Zlib::Zlib() {
    // Fidèle a GXD_DeviceCreate : LoadLibrary("GXDCompress.dll") + GetProcAddress.
    HMODULE h = LoadLibraryA("GXDCompress.dll");
    dll_ = h;
    if (!h) {
        TS2_ERR("GXDCompress.dll introuvable — la decompression zlib est indisponible.");
        return;
    }
    uncompress_ = reinterpret_cast<UncompressFn>(GetProcAddress(h, "uncompress"));
    if (!uncompress_)
        TS2_ERR("uncompress introuvable dans GXDCompress.dll.");
}

Zlib::~Zlib() {
    if (dll_) FreeLibrary(static_cast<HMODULE>(dll_));
}

bool Zlib::Inflate(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t rawSize) {
    if (!uncompress_) return false;
    unsigned long destLen = static_cast<unsigned long>(rawSize);
    int rc = uncompress_(dst, &destLen, src, static_cast<unsigned long>(srcLen));
    return rc == 0 /*Z_OK*/ && destLen == rawSize;
}

std::vector<uint8_t> Zlib::InflateTo(const uint8_t* src, size_t srcLen, size_t rawSize) {
    std::vector<uint8_t> out(rawSize);
    if (!Inflate(src, srcLen, out.data(), rawSize))
        throw AssetError("Echec de decompression zlib (Z_OK/taille)");
    return out;
}

std::vector<uint8_t> Zlib::DecodeEnvelope(const uint8_t* data, size_t size, size_t headerExtra) {
    ByteReader r(data, size);
    r.Skip(headerExtra);
    const uint32_t rawSize = r.U32();
    const uint32_t packed  = r.U32();
    if (r.Remaining() < packed)
        throw AssetError("Enveloppe zlib : packedSize depasse le fichier");
    return InflateTo(r.Ptr(), packed, rawSize);
}

} // namespace ts2::asset
