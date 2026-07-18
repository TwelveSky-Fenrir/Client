// Audio/OggVorbisDecoder.h — Ogg Vorbis decoder -> 16-bit stereo 44100 PCM.
//
// === IDA source (idaTs2 — sole source of truth): Snd_LoadOggToBuffers 0x6A8120 ===
//   The binary decodes the Ogg via libvorbisfile then fills the DirectSound buffers.
//   The exact decode path replicated here:
//     * OggVF_Open 0x6BFB60   (ov_open) -> we use ov_open_callbacks over memory.
//     * Format guard: rejects anything that is NOT  vi->channels == 2  AND
//       vi->rate == 44100  (the binary does `goto LABEL_44` -> failure).
//     * OggVF_PcmTotal 0x6BD7C0 (ov_pcm_total): pre-sizes  frames * 4  bytes
//       (stereo x 16-bit = 4 bytes/frame). The binary fails if the total is 0.
//     * OggVF_Read 0x6BEAF0 (ov_read): buffer 4096, bigendianp=0, word=2 (16-bit),
//       sgned=1. n==0 -> EOF (success); n<0 -> failure (the binary bails out).
//     * OggVF_Clear 0x6BD4C0 (ov_clear): frees the decoder.
//
// The produced PCM (interleaved 16-bit stereo 44100) then feeds AudioSystem via the
// PcmLoadCallback callback. Namespace ts2::audio.
//
// === VeryOldClient indicator (different/altered build — NAMES/idioms only) ===
//   ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG (Core/GXD/SOUNDDATA_FOR_GXD.cpp) decodes
//   the OGG inline (ov_open/ov_info/ov_pcm_total/ov_read/ov_clear) in the SAME method as
//   the buffer upload. ClientSource splits decode/upload into two units (structural choice, PLAUSIBLE).
//   Accepted IO difference: VeryOld opens a FILE* (fopen "rb" + _setmode _O_BINARY(0x8000) + ov_open);
//   ClientSource reads in memory (ov_open_callbacks). IO detail not ported, identical PCM.
//   Detailed statuses: Docs/TS2_AUDIO_ROSETTA.md §B.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::audio {

// Metadata of the decoded stream (filled after a successful decode).
struct OggDecodeInfo {
    int      channels    = 0;   // vi->channels (always 2 after the format guard)
    long     sampleRate  = 0;   // vi->rate     (always 44100 after the format guard)
    uint64_t totalFrames = 0;   // ov_pcm_total(-1): number of stereo frames
};

// Decodes an Ogg Vorbis IN MEMORY to 16-bit stereo 44100 PCM (mirror of
// Snd_LoadOggToBuffers 0x6A8120). Rejects any stream that is not 2 channels / 44100 Hz.
// `out` receives the interleaved PCM. Returns false if opening, the format guard, or
// an ov_read fails. `info` (optional) receives the stream metadata.
bool DecodeOggVorbisToPcm16(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& out, OggDecodeInfo* info = nullptr);

// Vector overload.
bool DecodeOggVorbisToPcm16(const std::vector<uint8_t>& data,
                            std::vector<uint8_t>& out, OggDecodeInfo* info = nullptr);

// PCM load callback compatible with ts2::audio::PcmLoadCallback:
//   bool(const std::string& path, std::vector<uint8_t>& outPcm).
// Reads the file via ts2::asset::ReadOggFile (checks "OggS") then decodes.
// To be wired into AudioSystem::SetLoadCallback.
bool OggVorbisLoadCallback(const std::string& path, std::vector<uint8_t>& out);

} // namespace ts2::audio
