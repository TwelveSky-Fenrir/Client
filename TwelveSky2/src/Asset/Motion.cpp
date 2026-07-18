// Asset/Motion.cpp — faithful translation of RE/asset_parsers/motion.py (validated 4527/4527).
#include "Asset/Motion.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cstring>

namespace ts2::asset {

namespace {

constexpr char   kMagic[6]     = { 'M', 'O', 'T', 'I', 'O', 'N' };
constexpr size_t kKeyframeSize = 28; // 7 floats on disk

// Parses the decompressed block, common to all 3 envelopes:
//   [count_A:u32][count_B:u32][ count_A*count_B keyframes of 28 bytes ]
// Throws AssetError if the body is inconsistent (mirrors body_ok of the validated parser)
// or if the buffer is truncated (via ByteReader).
// MOTION encryption = NONE (CONFIRMED, 4527/4527 inflate plain OK).
// ex-VeryOldClient: MOTION2_FOR_GXD.cpp — "NO XXTEA. Zlib only".
void ParseBody(const std::vector<uint8_t>& dec,
               uint32_t& frameCount, uint32_t& boneCount,
               std::vector<MotionKeyframe>& out) {
    ByteReader r(dec);
    frameCount = r.U32(); // count_A
    boneCount  = r.U32(); // count_B

    // 64-bit multiplication to avoid any u32 overflow.
    const uint64_t nKey = static_cast<uint64_t>(frameCount) * boneCount;

    // Strict consistency: 8 + n*28 == rawSize (validated byte-exact on 4527 files).
    const uint64_t expected = 8ull + nKey * kKeyframeSize;
    if (expected != dec.size())
        throw AssetError("MOTION: inconsistent body (8 + count_A*count_B*28 != rawSize)");

    out.clear();
    out.reserve(static_cast<size_t>(nKey));
    for (uint64_t i = 0; i < nKey; ++i) {
        MotionKeyframe k;
        k.qx = r.F32(); k.qy = r.F32(); k.qz = r.F32(); k.qw = r.F32(); // quaternion
        k.tx = r.F32(); k.ty = r.F32(); k.tz = r.F32();                 // translation
        out.push_back(k);
    }
}

} // namespace

bool Motion::Load(const std::string& path) {
    envelope_   = MotionEnvelope::Unknown;
    version_    = 0;
    frameCount_ = boneCount_ = 0;
    keyframes_.clear();

    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("MOTION: cannot open: %s", path.c_str());
        return false;
    }
    if (data.size() < 12) { // guard from the Python parser
        TS2_ERR("MOTION: file too short (%zu bytes): %s", data.size(), path.c_str());
        return false;
    }

    try {
        std::vector<uint8_t> dec;

        if (std::memcmp(data.data(), kMagic, sizeof(kMagic)) == 0) {
            // ---- Envelope with "MOTION" magic ----
            // The version is an ASCII digit read via atoi ('2' or '3').
            const char vc = static_cast<char>(data[6]);
            if (vc < '0' || vc > '9')
                throw AssetError("MOTION: non-numeric version");
            version_ = vc - '0';

            if (version_ == 3) {
                // MOTION3: sub-header (type=1, ver<=1, 2 reserved bytes) then zlib envelope.
                // Motion_ReadSubHeader 0x40B130. ex-VeryOldClient: MOTION2_FOR_GXD.cpp::LoadCompressedChunk.
                envelope_ = MotionEnvelope::Motion3;
                const uint8_t subType = data[7];
                const uint8_t subVer  = data[8];
                if (subType != 1 || subVer > 1)
                    TS2_WARN("MOTION3: unexpected sub-header (type=%u ver=%u): %s",
                             subType, subVer, path.c_str());
                // Layout: magic(6)+ver(1)+subType(1)+subVer(1)+reserved(2) = 11 bytes,
                // then rawSize@11, packedSize@15, zlib stream@19 => headerExtra = 11.
                dec = Zlib::Instance().DecodeEnvelope(data.data(), data.size(), /*headerExtra*/ 11);
            } else if (version_ == 2) {
                // MOTION2: NO zlib. The [count_A][count_B][keyframes] block starts
                // at offset 7 (15-byte logical header = offset 7 + the two count u32s).
                // Motion_ReadFrames 0x40B1A0. ex-VeryOldClient: MOTION2_FOR_GXD.cpp::LoadUncompressedChunk.
                envelope_ = MotionEnvelope::Motion2;
                dec.assign(data.begin() + 7, data.end());
            } else {
                throw AssetError("MOTION: unexpected version");
            }
        } else {
            // ---- RAW envelope without magic: [rawSize:u32][packedSize:u32][zlib stream] ----
            // Anim_ReadMotionStream 0x43CDB0. ex-VeryOldClient: ZlibScope.h (shared framing).
            envelope_ = MotionEnvelope::Raw;
            version_  = 3;
            if (data[8] != 0x78) // expected zlib header (78 xx) at offset 8
                throw AssetError("MOTION RAW: no zlib header at offset 8");
            dec = Zlib::Instance().DecodeEnvelope(data.data(), data.size(), /*headerExtra*/ 0);
        }

        ParseBody(dec, frameCount_, boneCount_, keyframes_);
        return true;
    } catch (const std::exception& ex) {
        TS2_ERR("MOTION: parse failed (%s): %s", ex.what(), path.c_str());
        envelope_   = MotionEnvelope::Unknown;
        version_    = 0;
        frameCount_ = boneCount_ = 0;
        keyframes_.clear();
        return false;
    }
}

const MotionKeyframe& Motion::At(uint32_t frame, uint32_t bone) const {
    if (frame >= frameCount_ || bone >= boneCount_)
        throw AssetError("MOTION: (frame,bone) index out of range");
    return keyframes_[static_cast<size_t>(frame) * boneCount_ + bone];
}

} // namespace ts2::asset
