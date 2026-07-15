// Asset/Motion.cpp — traduction fidèle de RE/asset_parsers/motion.py (validé 4527/4527).
#include "Asset/Motion.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cstring>

namespace ts2::asset {

namespace {

constexpr char   kMagic[6]     = { 'M', 'O', 'T', 'I', 'O', 'N' };
constexpr size_t kKeyframeSize = 28; // 7 floats sur disque

// Analyse le bloc décompressé, commun aux 3 enveloppes :
//   [count_A:u32][count_B:u32][ count_A*count_B keyframes de 28 o ]
// Lève AssetError si le corps est incohérent (miroir de body_ok du parseur validé)
// ou si le buffer est tronqué (via ByteReader).
void ParseBody(const std::vector<uint8_t>& dec,
               uint32_t& frameCount, uint32_t& boneCount,
               std::vector<MotionKeyframe>& out) {
    ByteReader r(dec);
    frameCount = r.U32(); // count_A
    boneCount  = r.U32(); // count_B

    // Multiplication en 64 bits pour éviter tout débordement u32.
    const uint64_t nKey = static_cast<uint64_t>(frameCount) * boneCount;

    // Cohérence stricte : 8 + n*28 == rawSize (validé byte-exact sur 4527 fichiers).
    const uint64_t expected = 8ull + nKey * kKeyframeSize;
    if (expected != dec.size())
        throw AssetError("MOTION : corps incoherent (8 + count_A*count_B*28 != rawSize)");

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
        TS2_ERR("MOTION : ouverture impossible : %s", path.c_str());
        return false;
    }
    if (data.size() < 12) { // garde-fou du parseur Python
        TS2_ERR("MOTION : fichier trop court (%zu o) : %s", data.size(), path.c_str());
        return false;
    }

    try {
        std::vector<uint8_t> dec;

        if (std::memcmp(data.data(), kMagic, sizeof(kMagic)) == 0) {
            // ---- Enveloppe avec magic "MOTION" ----
            // La version est un chiffre ASCII lu via atoi ('2' ou '3').
            const char vc = static_cast<char>(data[6]);
            if (vc < '0' || vc > '9')
                throw AssetError("MOTION : version non numerique");
            version_ = vc - '0';

            if (version_ == 3) {
                // MOTION3 : sous-en-tête (type=1, ver<=1, 2 o réservés) puis enveloppe zlib.
                envelope_ = MotionEnvelope::Motion3;
                const uint8_t subType = data[7];
                const uint8_t subVer  = data[8];
                if (subType != 1 || subVer > 1)
                    TS2_WARN("MOTION3 : sous-en-tete inattendu (type=%u ver=%u) : %s",
                             subType, subVer, path.c_str());
                // Layout : magic(6)+ver(1)+subType(1)+subVer(1)+reserve(2) = 11 o,
                // puis rawSize@11, packedSize@15, flux zlib@19 => headerExtra = 11.
                dec = Zlib::Instance().DecodeEnvelope(data.data(), data.size(), /*headerExtra*/ 11);
            } else if (version_ == 2) {
                // MOTION2 : PAS de zlib. Le bloc [count_A][count_B][keyframes] commence
                // à l'offset 7 (en-tête logique 15 o = offset 7 + les deux u32 de comptage).
                envelope_ = MotionEnvelope::Motion2;
                dec.assign(data.begin() + 7, data.end());
            } else {
                throw AssetError("MOTION : version inattendue");
            }
        } else {
            // ---- Enveloppe RAW sans magic : [rawSize:u32][packedSize:u32][flux zlib] ----
            envelope_ = MotionEnvelope::Raw;
            version_  = 3;
            if (data[8] != 0x78) // en-tête zlib attendu (78 xx) à l'offset 8
                throw AssetError("MOTION RAW : pas d'en-tete zlib a l'offset 8");
            dec = Zlib::Instance().DecodeEnvelope(data.data(), data.size(), /*headerExtra*/ 0);
        }

        ParseBody(dec, frameCount_, boneCount_, keyframes_);
        return true;
    } catch (const std::exception& ex) {
        TS2_ERR("MOTION : parse echoue (%s) : %s", ex.what(), path.c_str());
        envelope_   = MotionEnvelope::Unknown;
        version_    = 0;
        frameCount_ = boneCount_ = 0;
        keyframes_.clear();
        return false;
    }
}

const MotionKeyframe& Motion::At(uint32_t frame, uint32_t bone) const {
    if (frame >= frameCount_ || bone >= boneCount_)
        throw AssetError("MOTION : index (frame,bone) hors limites");
    return keyframes_[static_cast<size_t>(frame) * boneCount_ + bone];
}

} // namespace ts2::asset
