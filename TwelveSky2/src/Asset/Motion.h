// Asset/Motion.h — lecteur du format d'animation squelettique .MOTION.
// Fidèle à RE/asset_parsers/motion.py (validé 4527/4527) et aux lecteurs IDA
// Motion_LoadFile 0x40AF10 / Motion_ReadHeaderMagic 0x40B090 /
// Motion_ReadSubHeader 0x40B130 / Anim_ReadMotionStream 0x43CDB0.
//
// Disposition disque (les 3 enveloppes se rejoignent sur un bloc décompressé
// commun) :
//   [count_A:u32][count_B:u32][ count_A*count_B keyframes de 28 o ]
// avec rawSize == 8 + count_A*count_B*28.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// Enveloppe disque détectée pour un fichier .MOTION.
enum class MotionEnvelope {
    Unknown,
    Motion3, // magic "MOTION" + version '3' : sous-en-tête 4 o + enveloppe zlib (en-tête 19 o)
    Motion2, // magic "MOTION" + version '2' : keyframes bruts NON compressés (en-tête 15 o)
    Raw,     // sans magic : [rawSize:u32][packedSize:u32][flux zlib] (en-tête 8 o)
};

// Keyframe décodée = 7 floats (28 o sur disque).
// Sur disque : quaternion de rotation (x,y,z,w) puis translation (x,y,z).
// À l'exécution le client la transforme en matrice 4x4 (Motion_QuatToMatrix 0x6BB684
// pour la rotation, puis m41/m42/m43 = translation).
// ex-VeryOldClient: MOTION_MATRIX (CONFIRMED) — D3DXQUATERNION mRotate + D3DXVECTOR3 mTrans (28 o).
struct MotionKeyframe {
    float qx = 0.f, qy = 0.f, qz = 0.f, qw = 0.f; // quaternion de rotation
    float tx = 0.f, ty = 0.f, tz = 0.f;           // translation
};

class Motion {
public:
    // Charge et décode un .MOTION (les 3 enveloppes). false si lecture/format invalide.
    bool Load(const std::string& path);

    MotionEnvelope Envelope() const { return envelope_; }
    int Version() const { return version_; }            // 2 ou 3

    // count_A / count_B du bloc décompressé.
    // count_A = nombre d'images (frames), count_B = nombre d'os (bones).
    uint32_t FrameCount() const { return frameCount_; } // count_A
    uint32_t BoneCount()  const { return boneCount_; }  // count_B
    uint32_t KeyframeCount() const { return static_cast<uint32_t>(keyframes_.size()); }

    // Keyframes en ordre disque = ordre mémoire du client (Anim_ReadMotionStream
    // les recopie linéairement). L'os est la dimension interne : les BoneCount()
    // keyframes d'une frame sont contiguës (palette d'os par frame).
    const std::vector<MotionKeyframe>& Keyframes() const { return keyframes_; }

    // Accès indexé : index = frame * BoneCount() + bone (bloc frame-major).
    // ex-VeryOldClient: MOTION_FOR_GXD.cpp mKeyMatrix[frame*bone] (CONFIRMED, indexation identique).
    // Lève AssetError (ByteReader.h) si (frame,bone) est hors limites.
    const MotionKeyframe& At(uint32_t frame, uint32_t bone) const;

private:
    MotionEnvelope envelope_ = MotionEnvelope::Unknown;
    int version_ = 0;
    uint32_t frameCount_ = 0; // count_A
    uint32_t boneCount_  = 0; // count_B
    std::vector<MotionKeyframe> keyframes_; // frameCount_ * boneCount_, ordre disque
};

} // namespace ts2::asset
