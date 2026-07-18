// Asset/Motion.h — reader for the .MOTION skeletal animation format.
// Faithful to RE/asset_parsers/motion.py (validated 4527/4527) and to the IDA readers
// Motion_LoadFile 0x40AF10 / Motion_ReadHeaderMagic 0x40B090 /
// Motion_ReadSubHeader 0x40B130 / Anim_ReadMotionStream 0x43CDB0.
//
// Disk layout (the 3 envelopes converge on a common decompressed block):
//   [count_A:u32][count_B:u32][ count_A*count_B keyframes of 28 bytes ]
// with rawSize == 8 + count_A*count_B*28.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// Disk envelope detected for a .MOTION file.
enum class MotionEnvelope {
    Unknown,
    Motion3, // magic "MOTION" + version '3': 4-byte sub-header + zlib envelope (19-byte header)
    Motion2, // magic "MOTION" + version '2': raw UNCOMPRESSED keyframes (15-byte header)
    Raw,     // no magic: [rawSize:u32][packedSize:u32][zlib stream] (8-byte header)
};

// Decoded keyframe = 7 floats (28 bytes on disk).
// On disk: rotation quaternion (x,y,z,w) then translation (x,y,z).
// At runtime the client turns it into a 4x4 matrix (Motion_QuatToMatrix 0x6BB684
// for the rotation, then m41/m42/m43 = translation).
// ex-VeryOldClient: MOTION_MATRIX (CONFIRMED) — D3DXQUATERNION mRotate + D3DXVECTOR3 mTrans (28 bytes).
struct MotionKeyframe {
    float qx = 0.f, qy = 0.f, qz = 0.f, qw = 0.f; // rotation quaternion
    float tx = 0.f, ty = 0.f, tz = 0.f;           // translation
};

class Motion {
public:
    // Loads and decodes a .MOTION (all 3 envelopes). false if read/format invalid.
    bool Load(const std::string& path);

    MotionEnvelope Envelope() const { return envelope_; }
    int Version() const { return version_; }            // 2 or 3

    // count_A / count_B of the decompressed block.
    // count_A = number of frames, count_B = number of bones.
    uint32_t FrameCount() const { return frameCount_; } // count_A
    uint32_t BoneCount()  const { return boneCount_; }  // count_B
    uint32_t KeyframeCount() const { return static_cast<uint32_t>(keyframes_.size()); }

    // Keyframes in disk order = client memory order (Anim_ReadMotionStream
    // copies them linearly). Bone is the inner dimension: the BoneCount()
    // keyframes of a frame are contiguous (per-frame bone palette).
    const std::vector<MotionKeyframe>& Keyframes() const { return keyframes_; }

    // Indexed access: index = frame * BoneCount() + bone (frame-major block).
    // ex-VeryOldClient: MOTION_FOR_GXD.cpp mKeyMatrix[frame*bone] (CONFIRMED, identical indexing).
    // Throws AssetError (ByteReader.h) if (frame,bone) is out of range.
    const MotionKeyframe& At(uint32_t frame, uint32_t bone) const;

private:
    MotionEnvelope envelope_ = MotionEnvelope::Unknown;
    int version_ = 0;
    uint32_t frameCount_ = 0; // count_A
    uint32_t boneCount_  = 0; // count_B
    std::vector<MotionKeyframe> keyframes_; // frameCount_ * boneCount_, disk order
};

} // namespace ts2::asset
