// Asset/Sound.h — TwelveSky2 audio reader.
//   .WSOUND  = per-zone ambient sound bank (METADATA container).
//   .ISN     = effects (D06_GSOUND)      -> raw Ogg Vorbis.
//   .BGM     = music (D10_WORLDBGM)      -> raw Ogg Vorbis.
//   *.OGG    = sounds extracted from the bank -> raw Ogg Vorbis.
//
// Faithful to RE/asset_parsers/wsound.py (validated 75/75).
//
// === IDA sources (sole source of truth) ===
//   WSndBank_LoadFile         0x4DA790  -> parses the .WSOUND container
//   WSndBank_UpdatePositional 0x4DAC30  -> 3D logic: layout of the 20-byte record
//   Snd_LoadOggToBuffers      0x6A8120  -> decodes an Ogg (ov_open, requires stereo/44100)
//   World_LoadZoneResource    0x4DCB60  -> path "G03_GDATA\D09_WSOUND\Z%03d\Z%03d.WSOUND"
//   External OGG (Crt_Vsnprintf)        -> "<base>_%04d.OGG", 1-based index
//
// === IMPORTANT ===
//   The .WSOUND container does NOT contain the Ogg data. Each sound is loaded
//   from an external file "<base>_%04d.OGG" (i=1..count). The 100-byte record
//   carries the source .ogg name + 36 bytes of stale runtime fields (the build
//   tool serialized live memory: stack/ntdll addresses). The load path only
//   relies on the index -> these fields are ignored at load time.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// Ogg page signature ("OggS").
inline constexpr char kOggMagic[4] = {'O', 'g', 'g', 'S'};

// True if the buffer starts with the Ogg page signature "OggS".
bool IsOgg(const uint8_t* data, size_t size);
inline bool IsOgg(const std::vector<uint8_t>& v) { return IsOgg(v.data(), v.size()); }

// Loads a raw Ogg Vorbis (.ISN / .BGM / .OGG) as-is into memory.
// Returns false if the file cannot be opened or the header isn't "OggS".
bool ReadOggFile(const std::string& path, std::vector<uint8_t>& out);

// Metadata of a sound (100-byte record at this+8). Only the name is used by
// the game; the remaining 36 bytes are stale runtime fields (see header banner).
struct WSoundEntry {
    std::string name;              // source ".ogg" name (64-byte buffer, null-terminated)
    std::vector<uint8_t> oggData;  // empty until LoadExternalOggs() has been called
                                   // (the .WSOUND container does NOT embed the audio)
};

// Positional emitter (20-byte record at this+20, used by WSndBank_UpdatePositional).
struct WSoundEmitter {
    uint32_t soundIndex = 0;  // 0-based, == index of the 100-byte record / OGG
    float    x = 0.0f;
    float    y = 0.0f;
    float    z = 0.0f;        // Math_Dist3D(rec+4, listener)
    float    radius = 0.0f;   // range; if dist < radius -> plays, volume ~ (radius-d)/radius
};

// .WSOUND container.
//
// Disk layout:
//   [u32 count]                number of sounds (> 0)            (this+8)
//   count  * { 100-byte record }  per-sound metadata
//   [u32 count2]               number of positional emitters     (this+16)
//   count2 * { 20-byte record }  3D placements                   (this+20)
//   Expected size == 4 + 100*count + 4 + 20*count2
class WSound {
public:
    // Parses the .WSOUND container (metadata + emitters). Does NOT load the audio.
    // Returns false if the structure is inconsistent (bounds/sizes).
    bool Load(const std::string& path);

    // Loads the external OGGs "<base>_%04d.OGG" (i=1..count) and fills
    // WSoundEntry::oggData. Returns the number of valid "OggS" OGGs loaded.
    // Does nothing until Load() has succeeded.
    size_t LoadExternalOggs();

    // Path of the external OGG for a 1-based index (e.g. "Z001.WSOUND_0001.OGG").
    std::string OggPathFor(size_t oneBasedIndex) const;

    const std::string&               Path()     const { return path_; }
    const std::vector<WSoundEntry>&  Entries()  const { return entries_; }
    const std::vector<WSoundEmitter>& Emitters() const { return emitters_; }

    uint32_t Count()  const { return count_; }   // number of sounds
    uint32_t Count2() const { return count2_; }  // number of emitters

    // Validation (mirrors the Python validator).
    bool   SizeOk()        const { return sizeOk_; }        // disk size == expected
    size_t ExpectedSize()  const { return expectedSize_; }
    size_t ActualSize()    const { return actualSize_; }
    size_t Trailing()      const { return trailing_; }      // bytes after the last record
    size_t BadIndexCount() const { return badIndex_; }      // emitters with soundIndex out of [0,count)

private:
    std::string                path_;
    std::vector<WSoundEntry>   entries_;
    std::vector<WSoundEmitter> emitters_;
    uint32_t count_  = 0;
    uint32_t count2_ = 0;
    bool     sizeOk_ = false;
    size_t   expectedSize_ = 0;
    size_t   actualSize_   = 0;
    size_t   trailing_     = 0;
    size_t   badIndex_     = 0;
};

} // namespace ts2::asset
